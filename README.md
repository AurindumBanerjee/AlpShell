# AlpShell

AlpShell is a small Unix shell written in C++17 by students of IIT Jodhpur. It reads commands with GNU readline,
parses them with its own lexer and parser, and runs them with `fork`/`execvp`. The `shell/` directory is the
project; the other top-level directories are unrelated experiments (see [Experiments](#experiments)).

This README describes what the code does today. Anything not listed under **What works** is not implemented.

## What works

Checked by running the build (the automated checks are in [shell/tests/](shell/tests/)):

* **Interactive and script use.** `alpshell` starts a prompt when stdin is a terminal. `alpshell script.sh` runs a
  file, and commands piped to stdin are run line by line. Lines starting with `#` are comments.
* **Running programs:** `cmd arg1 arg2` through `execvp`, found on `PATH`.
* **Pipelines and redirection:** `a | b | c`, `< file`, `> file`, `>> file`.
* **Quoting:** `'single quotes'` are literal. `"double quotes"` keep spaces but still expand variables.
* **Expansion:** `$VAR` and `${VAR}` (read from the environment), and `*`, `?`, `[...]` globbing through `glob(3)`.
  Nothing is expanded inside single quotes.
* **Job control (interactive shell only):**
  * every pipeline runs as a job in its own process group, and the foreground job owns the terminal;
  * Ctrl+C kills the foreground job and not the shell; Ctrl+Z stops it and keeps it in the job table;
  * `jobs`, `fg [%n]`, `bg [%n]` and `kill [-SIG] pid|%n`;
  * a trailing `&` starts a background job, and finished or stopped jobs are reported before the next prompt;
  * a pipeline whose `fork` or `pipe` fails midway kills and reaps the processes it already started.
  A shell that is not interactive (scripts, piped stdin) does not touch process groups or the terminal, like bash.
* **Other builtins:** `cd`, `history` (interactive only), and `exit` when it is the only thing on the line.
  A line that is exactly `ls` runs `ls --color=auto`.
* **Line editing:** readline editing, Tab completion (commands from `PATH` for the first word, file names elsewhere),
  and a red `$` in the prompt when the current line has a syntax problem.

## Known problems

These are bugs in the current code, not planned features. The first was reproduced by running the build; the other
two were found by reading the lexer and parser.

* `exit 3` does not exit (only a bare `exit` does), and no command ever reports an exit status.
* Operators glued to words are not split: `sleep 5&` is one word `5&`, not a background job.
* A quoted `">"` is treated as a redirection, and `a"b"c` is split into three words.

## Not implemented yet

Planned, not working: `;`, `&&`, `||`, `2>`, `2>&1`, backslash escapes, `$?`, `~` expansion, `export`, `unset`,
`echo` and `pwd` as builtins, aliases, shell variables other than the environment, functions, conditionals, loops, and
the directory stack (`pushd`, `popd`).

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
