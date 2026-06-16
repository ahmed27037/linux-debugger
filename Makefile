CC = gcc
CFLAGS = -Wall -g

# The debugger itself. Links against elfutils' libdw for DWARF parsing.
# Build dependency (Debian/Ubuntu): sudo apt install libdw-dev
all: debugger target

debugger: main.c
	$(CC) $(CFLAGS) -o debugger main.c -ldw

# Sample tracee. Built with -no-pie so its symbol addresses are fixed,
# which is what the debugger's DWARF line->address lookup assumes.
target: target.c
	$(CC) -no-pie -g -o target target.c

clean:
	rm -f debugger target

.PHONY: all clean
