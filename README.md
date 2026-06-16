# linux-debugger

A small source-level debugger for x86-64 Linux, written from scratch in C. It drives a
target process with `ptrace(2)`, reads DWARF debug info through elfutils' `libdw` to map
source lines and local variables to addresses, and implements software breakpoints by
patching `INT3` (`0xCC`) into the tracee's text.

This is a learning / portfolio project: a single translation unit, no external debugger
libraries beyond `libdw` for DWARF parsing. The goal was to understand how a debugger
actually works at the kernel-interface level, not to reimplement GDB.

## Features

- **Process control** — forks the tracee, which calls `PTRACE_TRACEME` and `execl`s the
  target, then drives it from the parent.
- **DWARF parsing** (`libdw`) — builds a line-number → address table from the line program,
  and walks the DIE tree (`DW_TAG_subprogram` → `DW_TAG_variable` / `DW_TAG_formal_parameter`)
  to recover local-variable frame offsets from `DW_AT_location` / `DW_OP_fbreg`.
- **Software breakpoints** — set/remove by source line. Sets a breakpoint by saving the
  original byte and writing `0xCC` with `PTRACE_POKETEXT`; removes it by restoring the byte.
- **Single-stepping** — one instruction at a time via `PTRACE_SINGLESTEP`.
- **Continue** — `PTRACE_CONT` to the next breakpoint, correctly stepping over and re-arming
  the breakpoint at the current instruction.
- **Inspection** — full general-purpose register dump (`PTRACE_GETREGS`), read memory at an
  arbitrary address, and read a local variable by name (`rbp + frame_offset`).

## Interface

The debugger launches `./target` and presents a numeric menu:

```
1 = add/remove breakpoints
2 = inspect values (register dump / address / variable by name)
3 = step to next line
4 = go until next breakpoint
```

Breakpoints are specified by **source line number** in `target.c`.

## Build

Requires GCC and elfutils' libdw development headers:

```sh
sudo apt install libdw-dev      # Debian / Ubuntu
make                            # builds ./debugger and ./target
```

`make` produces two binaries:
- `debugger` — the debugger (`gcc -Wall -g main.c -ldw`)
- `target` — the sample tracee, built with `-no-pie -g` (see Limitations)

## Run

```sh
./debugger
```

Example session: choose `1` → `1` to add a breakpoint at line 5 of `target.c`, `4` to run
to it, `2` → `3` and enter `x` to read the local, `4` to continue, and so on until the
target exits.

## Limitations

These are deliberate scoping choices for a learning project, not bugs:

- **Linux x86-64 only.** Depends on `ptrace`, ELF/DWARF, and the x86-64 register layout.
- **Tracee is hardcoded to `./target`.** To debug other code, edit the `execl`/`load_symbols`
  calls and rebuild.
- **`-no-pie` required.** The line→address lookup uses link-time addresses, so the tracee
  must be built non-PIE (no PIE/ASLR address translation is done).
- **Variable inspection assumes `int` locals** at `DW_OP_fbreg` offsets, with a frame base of
  `CFA = RBP + 16` (frame-pointer-based code, as emitted by `gcc -g` without
  `-fomit-frame-pointer`).

## License

MIT — see [LICENSE](LICENSE).
