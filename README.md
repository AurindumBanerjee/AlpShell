# AlpShell

AlpShell is a small Unix shell written in C++17 by students of IIT Jodhpur. It reads commands with GNU readline,
parses them with its own lexer and parser, and runs them with `fork`/`execvp`. The `shell/` directory is the
project; the other top-level directories are unrelated experiments (see [Experiments](#experiments)).

This README describes what the code does today and what the tests check. Anything not listed under **What works**
is not implemented. It runs on Linux only (it uses `fork`, `termios` and `glob(3)`).

## What works

* **Three ways to run it:** `alpshell` (an interactive prompt when stdin is a terminal, otherwise it runs stdin),
  `alpshell script.sh`, and `alpshell -c 'command'`.
* **Command lists:** `a ; b`, `a && b`, `a || b`, evaluated left to right as in bash, and `a &` to run in the
  background (also `a && b &`, which runs the whole list in a background subshell).
* **Pipelines:** `a | b | c`. The status of a pipeline is the status of its last command.
* **Redirection:** `< file`, `> file`, `>> file`, `2> file`, `2>> file`, `2>&1`, `>&2`, applied left to right, so
  `cmd > f 2>&1` and `cmd 2>&1 > f` differ as in bash. A command may consist only of redirections (`> f` creates `f`).
* **Quoting:** `'single quotes'` are literal; `"double quotes"` keep spaces and still expand `$VAR`; a backslash
  escapes the next character (inside double quotes only `$ " \` and a newline); `a"b c"d` is one word; `''` is an
  empty argument. Operators inside quotes are ordinary text.
* **Expansion**, applied after the quoting rules, so nothing is expanded inside single quotes:
  * `$VAR`, `${VAR}`, `$?` (status of the last foreground pipeline) and `$!` (pid of the last background job);
  * `~` and `~/path` at the start of a word;
  * the result of an unquoted `$VAR` is split into words at spaces and tabs;
  * `*`, `?` and `[...]` match file names (`glob(3)`); a pattern that matches nothing stays as written, and a
    quoted wildcard is literal.
* **Variables:** `NAME=value` sets a shell variable, `NAME=value command` sets it for that command only, and
  `export` / `unset` manage the environment that children inherit.
* **Exit status:** every command reports one (`127` command not found, `126` found but not executable, `128+n`
  killed by signal `n`). The shell's own exit status is that of the last command, or the argument of `exit`.
* **Builtins:** `cd` (including `cd -`), `pwd`, `echo` (with `-n`), `export`, `unset`, `exit [n]`, `history`, `wait`,
  `jobs`, `fg`, `bg`, `kill`. A builtin inside a pipeline or in the background runs in a child process.
* **Job control (interactive shell only):**
  * every pipeline runs as a job in its own process group, and the foreground job owns the terminal;
  * Ctrl+C kills the foreground job and not the shell (`$?` becomes 130); Ctrl+Z stops it and keeps it in the job
    table (`$?` becomes 148); at the prompt Ctrl+C discards the line being typed;
  * `fg [%n]`, `bg [%n]`, `kill [-SIG] pid|%n` (also wakes a stopped job so it can die), `jobs`;
  * finished and stopped jobs are reported before the next prompt;
  * a pipeline whose `fork` or `pipe` fails midway kills and reaps the processes it already started.
  A shell that is not interactive (scripts, `-c`, piped stdin) leaves process groups and the terminal alone, like
  bash; a background command there reads from `/dev/null`.
* **Input handling:** a line that ends inside a quote, after a backslash, or after `|`, `&&` or `||` continues on the
  next line (the prompt becomes `> `). Syntax errors are reported with a message and set `$?` to 2; an interactive
  shell goes on, a script stops.
* **Line editing:** readline editing; one history store (readline's), loaded from and saved to
  `~/.alpshell_history` (override with `ALPSHELL_HISTFILE`); Tab completion of builtins and `PATH` commands in command
  position and file names elsewhere; the `$` of the prompt turns red while the line does not parse.

## Differences from bash

These are known and intentional (each has a case in [shell/tests/script_cases.txt](shell/tests/script_cases.txt)
that pins down what AlpShell does instead):

* `echo` only knows `-n` (no `-e`, `-E`).
* `$1`…`$9` expand to nothing; `$#`, `$@`, `$*`, `$-` are not special; `${VAR:-x}` and other `${...}` operators are
  an error ("bad substitution"); `~user` is not expanded; there is no command substitution (`$(...)`, backticks).
* No `if`, `for`, `while`, `case`, functions, subshells `( )` or groups `{ }`, here-documents (`<<`), aliases or
  `source`; those words are looked up as ordinary commands.
* A script is stopped by a syntax error. Error messages differ in wording for syntax errors and for `wait` with an
  unknown pid, and AlpShell prints no "Killed"/"Terminated" notices for background jobs in scripts.
* Job control exists only for an interactive shell.

## Planned

Aliases, `source`, command substitution, `$1`/`$#`/`$@`, parameter operators, control flow and functions,
here-documents, subshells, the directory stack (`pushd`, `popd`), `echo -e`.

## Build and run

Needs a C++17 compiler, GNU readline with headers, and GNU make or CMake 3.10+. On Debian or Ubuntu:
`sudo apt install g++ make cmake libreadline-dev python3`.

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

`make asan` builds `build-asan/alpshell` with AddressSanitizer and UBSan.

## Tests

```bash
cd shell
make test          # unit, script (against bash), command-line, pty and fork-failure tests
make test-asan     # the same on the sanitizer build
ctest --test-dir build-cmake --output-on-failure   # the same through CMake
```

* **Unit tests** (`tests/unit_tests.cpp`): lexer, parser, expansion and variables.
* **Script tests** (`tests/script_cases.txt`): each case runs under `bash` and under `alpshell`, non-interactively, and
  stdout, stderr and the exit status must match. Intentional differences are listed in the case file with the
  output AlpShell is expected to produce. Needs `bash` and `python3`.
* **Command-line tests** (`tests/cli_test.py`): `-c`, script files, commands on stdin, exit codes, usage errors.
* **Pty tests** (`tests/pty_tests.py`): the shell on a pseudo-terminal: Ctrl+C, Ctrl+Z, `fg`, `bg`, `kill`, job
  notices, `$?` after a signal, history, continuation lines, Tab completion, the red `$`, exit codes. They also
  look at the real process state in `/proc`.
* **Fork-failure tests** (`tests/fork_failure_test.py`): an `LD_PRELOAD` shim makes one `fork` or `pipe` call fail and
  checks that the processes already started are killed and reaped.

CI ([.github/workflows/ci.yml](.github/workflows/ci.yml)) builds with make and with cmake, runs all of the above, and
runs them again under AddressSanitizer and UBSan.

## Layout

```text
shell/            the shell: main.cpp, src/, include/, tests/, Makefile, CMakeLists.txt
bootloader/       experiment: 16-bit x86 boot sector (see below)
drivers/ kernel/ userland/    empty placeholders for a possible OS project
```

## Experiments

`bootloader/`, `drivers/`, `kernel/` and `userland/` are **not part of the shell** and are not built or tested.
`bootloader/src/main.asm` is a boot-sector experiment assembled with `nasm`; `drivers/`, `kernel/` and `userland/`
contain only empty READMEs. `bootloader/main.asm` is an older, separate draft whose code looks broken (for example `jmp.loop` jumps over the
character-printing instructions); nothing here has been assembled or run.

## Authors

- Aurindum Banerjee (B23CS1006)
- Lokesh Motwani (B23CS1033)
- Laksh Mendpara (B23CS1037)
- Aayush Bade (B23CS1084)
- Prashasta Srivastava (B23CS1096)

## References
