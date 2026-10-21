# AlpShell

AlpShell is a small Unix shell written in C++17 by students of IIT Jodhpur. It reads commands with GNU readline,
parses them with its own lexer and parser, and runs them with `fork`/`execvp`. The `shell/` directory is the
project; the other top-level directories are unrelated experiments (see [Experiments](#experiments)).

This README describes what the code does today. Anything not listed under **What works** is not implemented.

## What works

Checked by running the current build:

* **Interactive and script use.** `alpshell` starts a prompt when stdin is a terminal. `alpshell script.sh` runs a
  file, and commands piped to stdin are run line by line. Lines starting with `#` are comments.
* **Running programs:** `cmd arg1 arg2` through `execvp`, found on `PATH`, waited for in the foreground.
* **Quoting:** `'single quotes'` are literal. `"double quotes"` keep spaces but still expand variables.
* **Expansion:** `$VAR` and `${VAR}` (read from the environment), and `*`, `?`, `[...]` globbing through `glob(3)`.
  Nothing is expanded inside single quotes.
* **Builtins:** `cd`, `history` (interactive only), `jobs`, and `exit` when it is the only thing on the line.
  A line that is exactly `ls` runs `ls --color=auto`.
* **Background jobs:** a trailing `&` starts the command in the background and records it in the job table.
* **Line editing:** readline editing, Tab completion (commands from `PATH` for the first word, file names elsewhere),
  and a red `$` in the prompt when the current line has a syntax problem.

## Known problems

These are bugs in the current code, not planned features. The first two were reproduced by running the build; the last
two were found by reading the code.

* **Pipes and redirection do not work.** The lexer drops the `|`, `<`, `>` and `>>` tokens, so
  `echo hi | tr a-z A-Z` runs `echo` with the arguments `hi tr a-z A-Z`, and `echo hi > f` creates no file.
* `exit 3` does not exit (only a bare `exit` does), and no command ever reports an exit status.
* A SIGCHLD handler reaps every child, so it can steal the status of a foreground command.
* Pipelines are not put in their own process group, so Ctrl+C and Ctrl+Z reach the shell too.

## Not implemented yet

Planned, not working: `;`, `&&`, `||`, `2>`, `2>&1`, backslash escapes, `$?`, `~` expansion, `fg`, `bg`, `kill`,
`export`, `unset`, `echo`, `pwd` as builtins, aliases, shell variables other than the environment, functions,
conditionals, loops, and the directory stack (`pushd`, `popd`).

## Build and run

Needs a C++17 compiler, GNU readline with headers, and GNU make or CMake 3.10+. On Debian or Ubuntu:
`sudo apt install g++ make cmake libreadline-dev`.

```bash
cd shell
make                  # builds build/alpshell
./build/alpshell
```

or with CMake:

```bash
cd shell
cmake -S . -B build-cmake
cmake --build build-cmake
./build-cmake/alpshell
```

`make asan` builds `build-asan/alpshell` with AddressSanitizer and UBSan. More detail is in [shell/README.md](shell/README.md).

## Layout

```
shell/            the shell: main.cpp, src/, include/, Makefile, CMakeLists.txt
parser_test/      hand-run lexer/parser experiment (to be replaced by real tests)
bootloader/       experiment: 16-bit x86 boot sector (see below)
drivers/ kernel/ userland/    empty placeholders for a possible OS project
```

## Experiments

`bootloader/`, `drivers/`, `kernel/` and `userland/` are **not part of the shell** and are not built or tested.
`bootloader/src/main.asm` is a boot-sector experiment assembled with `nasm`; `drivers/`, `kernel/` and `userland/`
contain only empty READMEs. `bootloader/main.asm` is an older, separate draft that does not assemble correctly.

## Authors

- Aurindum Banerjee (B23CS1006)
- Lokesh Motwani (B23CS1033)
- Laksh Mendpara (B23CS1037)
- Aayush Bade (B23CS1084)
- Prashasta Srivastava (B23CS1096)

## References
