#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <fcntl.h>
#include <elfutils/libdw.h>
#include <string.h>
#include <dwarf.h>

// data structures & algorithms for data structures
struct breakpoint {
	long address;
	long instruction;
};

struct breakpoint *breakpoints = NULL;
int size = 5;
int capacity = 0;

void resize(struct breakpoint **arr, int *size, int capacity) {
	struct breakpoint *breakpoints_new = malloc(2 * (*size) * sizeof(struct breakpoint));
	for (int i = 0; i < capacity; i++) {
		breakpoints_new[i] = (*arr)[i];
	}
	free(*arr);
	*arr = breakpoints_new;
	*size = *size * 2;
}

void array_add(struct breakpoint **arr, int *size, int *capacity, long instruction, long address) {
	if (*capacity == *size) resize(arr, size, *capacity);
	struct breakpoint entry;
	entry.address = address;
	entry.instruction = instruction;
	(*arr)[*capacity] = entry;
	*capacity = *capacity + 1;
}

int search(struct breakpoint *arr, int capacity, long address) {
	int i = 0;
	while (i < capacity && arr[i].address != address) {
		i = i + 1;
	}
	if (i == capacity) {
		return -1;
	}
	else return i;
}

void array_remove(struct breakpoint *arr, int *capacity, long address) {
	int i = search(arr, *capacity, address);
	if (i == -1) return;
	while (i < *capacity - 1) {
		arr[i] = arr[i + 1];
		i = i + 1;
	}
	*capacity = *capacity - 1;
}



// data structure for lines


// The elfutils project docs and  dwarfstd.org used for reference
struct line_entry {
	int line;
	long address;
};

struct line_entry *line_table = NULL;
int line_table_size = 0;

struct variable_entry {
	char name[64];
	int frame_offset;
};

struct variable_entry *var_table = NULL;
int var_table_size = 0;


void load_symbols(const char *filename) {
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return;
	}

	Dwarf *dwarf = dwarf_begin(fd, DWARF_C_READ);
	if (dwarf == NULL) {
		printf("No debug info found. Did you compile with -g?\n");
		close(fd);
		return;
	}

	// first pass: count lines
	int count = 0;
	Dwarf_Off offset = 0;
	Dwarf_Off next_offset;
	size_t header_size;
	while (dwarf_nextcu(dwarf, offset, &next_offset, &header_size, NULL, NULL, NULL) == 0) {
		Dwarf_Die die;
		dwarf_offdie(dwarf, offset + header_size, &die);
		Dwarf_Lines *lines;
		size_t nlines;
		if (dwarf_getsrclines(&die, &lines, &nlines) == 0) {
			count += nlines;
		}
		offset = next_offset;
	}

	// allocate
	line_table = malloc(count * sizeof(struct line_entry));
	var_table = malloc(64 * sizeof(struct variable_entry));

	// second pass: fill line table and variable table
	offset = 0;
	int line_idx = 0;
	int var_idx = 0;
	while (dwarf_nextcu(dwarf, offset, &next_offset, &header_size, NULL, NULL, NULL) == 0) {
		Dwarf_Die cu_die;
		dwarf_offdie(dwarf, offset + header_size, &cu_die);

		// fill line table
		Dwarf_Lines *lines;
		size_t nlines;
		if (dwarf_getsrclines(&cu_die, &lines, &nlines) == 0) {
			for (size_t i = 0; i < nlines; i++) {
				Dwarf_Line *line = dwarf_onesrcline(lines, i);
				int lineno;
				Dwarf_Addr addr;
				dwarf_lineno(line, &lineno);
				dwarf_lineaddr(line, &addr);
				line_table[line_idx].line = lineno;
				line_table[line_idx].address = (long)addr;
				line_idx++;
			}
		}

		// walk DIE tree for variables
		Dwarf_Die child;
		if (dwarf_child(&cu_die, &child) == 0) {
			do {
				if (dwarf_tag(&child) == DW_TAG_subprogram) {
					Dwarf_Die var_die;
					if (dwarf_child(&child, &var_die) == 0) {
						do {
							int tag = dwarf_tag(&var_die);
							if (tag == DW_TAG_variable || tag == DW_TAG_formal_parameter) {
								const char *name = dwarf_diename(&var_die);
								Dwarf_Attribute loc_attr;
								if (name && dwarf_attr(&var_die, DW_AT_location, &loc_attr)) {
									Dwarf_Op *expr;
									size_t expr_len;
									if (dwarf_getlocation(&loc_attr, &expr, &expr_len) == 0 && expr_len > 0) {
										if (expr[0].atom == DW_OP_fbreg) {
											int fbreg_offset = (int)expr[0].number;
											// CFA = RBP + 16 on x86-64, so real offset = 16 + fbreg_offset
											var_table[var_idx].frame_offset = 16 + fbreg_offset;
											strncpy(var_table[var_idx].name, name, 63);
											var_table[var_idx].name[63] = '\0';
											var_idx++;
										}
									}
								}
							}
						} while (dwarf_siblingof(&var_die, &var_die) == 0);
					}
				}
			} while (dwarf_siblingof(&child, &child) == 0);
		}

		offset = next_offset;
	}

	line_table_size = line_idx;
	var_table_size = var_idx;

	dwarf_end(dwarf);
	close(fd);

	printf("Loaded %d line entries and %d variables from %s\n", line_table_size, var_table_size, filename);
}


long lookup_address(int lineno) {
	for (int i = 0; i < line_table_size; i++) {
		if (line_table[i].line == lineno) {
			return line_table[i].address;
		}
	}
	return -1;
}

// end reference




// end data structures & algorithms for data structures

// functions
int sendcommands() {
	printf("\n");
	printf("Choose next command \n");
	printf("1 = add/remove breakpoints, 2 = inspect values, 3 = step to next line, 4 = go until next breakpoint \n");
	int result = 0;
	scanf("%d", &result);
	while (result != 1 && result != 2 && result != 3 && result != 4) {
		while (getchar() != '\n');  // flush bad input
		printf("Not one of the options try again \n");
		printf("1 = add/remove breakpoints, 2 = inspect values, 3 = step to next line, 4 = go until next breakpoint \n");
		scanf("%d", &result);
	}
	return result;
}

void add_breakpoint(int pid) {
	int lineno = 0;
	printf("Enter line number: ");
	scanf("%d", &lineno);

	long address = lookup_address(lineno);
	if (address == -1) {
		printf("No address found for line %d\n", lineno);
		return;
	}

	long instruction = ptrace(PTRACE_PEEKTEXT, pid, (void *)address, NULL);
	long modified = (instruction & 0xFFFFFFFFFFFFFF00) | 0xCC;
	ptrace(PTRACE_POKETEXT, pid, (void *)address, (void *)modified);

	array_add(&breakpoints, &size, &capacity, instruction, address);
	printf("Breakpoint set at line %d (address 0x%lx)\n", lineno, address);
}


void remove_breakpoint(int pid) {
	int lineno = 0;
	printf("Enter line number: ");
	scanf("%d", &lineno);

	long address = lookup_address(lineno);
	if (address == -1) {
		printf("No address found for line %d\n", lineno);
		return;
	}

	int i = search(breakpoints, capacity, address);
	if (i == -1) {
		printf("error: line %d doesn't have a breakpoint\n", lineno);
		return;
	}
	long instruction = breakpoints[i].instruction;
	array_remove(breakpoints, &capacity, address);
	ptrace(PTRACE_POKETEXT, pid, (void *)address, (void *)instruction);
}



void promptbreakpoints(int pid) {
	int result = 0;
	printf("1- for adding breakpoints, 2- for removing breakpoints \n");
	while (result != 1 && result != 2) {
		scanf("%d", &result);
		if (result == 1) add_breakpoint(pid);
		else if (result == 2) remove_breakpoint(pid);
		else printf("Please type in one of the options correctly : 1- for adding breakpoints, 2- for removing breakpoints \n");
	}
}




void inspect_values(int pid) {
	struct user_regs_struct regs;
	ptrace(PTRACE_GETREGS, pid, NULL, &regs);

	printf("1- register dump, 2- address value, 3- variable by name\n");
	int result = 0;

	while (result != 1 && result != 2 && result != 3) {
		scanf("%d", &result);
		if (result == 1) {
			printf("--- Registers ---\n");
			printf("RIP: 0x%llx\n", regs.rip);
			printf("RSP: 0x%llx\n", regs.rsp);
			printf("RBP: 0x%llx\n", regs.rbp);
			printf("RAX: 0x%llx\n", regs.rax);
			printf("RBX: 0x%llx\n", regs.rbx);
			printf("RCX: 0x%llx\n", regs.rcx);
			printf("RDX: 0x%llx\n", regs.rdx);
			printf("RSI: 0x%llx\n", regs.rsi);
			printf("RDI: 0x%llx\n", regs.rdi);
			printf("R8:  0x%llx\n", regs.r8);
			printf("R9:  0x%llx\n", regs.r9);
			printf("R10: 0x%llx\n", regs.r10);
			printf("R11: 0x%llx\n", regs.r11);
			printf("R12: 0x%llx\n", regs.r12);
			printf("R13: 0x%llx\n", regs.r13);
			printf("R14: 0x%llx\n", regs.r14);
			printf("R15: 0x%llx\n", regs.r15);
			printf("EFLAGS: 0x%llx\n", regs.eflags);
			return;
		}
		else if (result == 2) {
			printf("Specify address in hex: ");
			long address;
			scanf("%lx", &address);
			long value = ptrace(PTRACE_PEEKTEXT, pid, (void *)address, NULL);
			printf("Value is 0x%lx\n", value);
			return;
		}
		else if (result == 3) {
			char name[64];
			printf("Enter variable name: ");
			scanf("%63s", name);
			int found = -1;
			for (int i = 0; i < var_table_size; i++) {
				if (strcmp(var_table[i].name, name) == 0) {
					found = i;
					break;
				}
			}
			if (found == -1) {
				printf("Variable '%s' not found\n", name);
				return;
			}
			long addr = regs.rbp + var_table[found].frame_offset;
			long raw = ptrace(PTRACE_PEEKTEXT, pid, (void *)addr, NULL);
			int int_value = (int)(raw & 0xFFFFFFFF);
			printf("%s = %d (at 0x%lx)\n", name, int_value, addr);
			return;
		}
		else printf("Please input 1, 2, or 3\n");
	}
}






















void step(int pid, int *status) {
	ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL);
	waitpid(pid, status, 0);
}

void continue_to_next(int pid, int *status) {
	// get PC
	struct user_regs_struct regs;
	ptrace(PTRACE_GETREGS, pid, NULL, &regs);

	// check if we are sitting on a breakpoint
	int index = search(breakpoints, capacity, regs.rip - 1);
	if (index != -1) {
		// restore original instruction and rewind PC
		regs.rip = regs.rip - 1;
		long original = breakpoints[index].instruction;
		ptrace(PTRACE_POKETEXT, pid, (void *)(regs.rip), (void *)original);
		ptrace(PTRACE_SETREGS, pid, NULL, &regs);

		// execute that one instruction
		long address = regs.rip;
		ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL);
		waitpid(pid, status, 0);

		// re-insert the breakpoint
		long masked = (original & 0xFFFFFFFFFFFFFF00) | 0xCC;
		ptrace(PTRACE_POKETEXT, pid, (void *)address, (void *)masked);

		// now continue to next breakpoint
		ptrace(PTRACE_CONT, pid, NULL, NULL);
		waitpid(pid, status, 0);
	}
	else {
		ptrace(PTRACE_CONT, pid, NULL, NULL);
		waitpid(pid, status, 0);
	}
}
// endfunctions

int main() {





	// initiate variables and data structures

	breakpoints = malloc(5 * sizeof(struct breakpoint));


	// PARSE EILF 
	load_symbols("./target");

	// startup
	int pid = fork();
	int status = 0;



	if (pid == 0) {
		ptrace(PTRACE_TRACEME, 0, NULL, NULL);
		execl("./target", "target", NULL);
	}
	else waitpid(pid, &status, 0);

	while (1) {
		if (WIFEXITED(status)) {
    			printf("Target exited with code %d\n", WEXITSTATUS(status));
    			break;
		}
		int result = sendcommands();
		if (result == 1) promptbreakpoints(pid);
		else if (result == 2) inspect_values(pid);
		else if (result == 3) step(pid, &status);
		else if (result == 4) continue_to_next(pid, &status);
	}

	return 0;
}
