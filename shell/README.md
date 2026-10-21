# shell/

The AlpShell source. See the [top-level README](../README.md) for what the shell can and cannot do.

## Build

Requires a C++17 compiler and GNU readline with its headers (`libreadline-dev` on Debian/Ubuntu). Linux only: the shell
uses `fork`, `exec`, `termios` and `glob(3)`. The tests also need `python3` and `bash`.

| Command | Result |
|---|---|
| `make` | `build/alpshell`, built with `-std=c++17 -Wall -Wextra` |
| `make asan` | `build-asan/alpshell`, built with AddressSanitizer and UBSan |
| `make test` | builds, then runs the unit, script, command-line, pty and fork-failure tests |
| `make test-asan` | the same tests against the sanitizer build |
| `make clean` | removes `build/` and `build-asan/` |
| `cmake -S . -B build-cmake && cmake --build build-cmake` | the same program through CMake |
| `cmake -S . -B build-cmake -DALPSHELL_SANITIZE=ON` | CMake build with the sanitizers |
| `ctest --test-dir build-cmake --output-on-failure` | all tests through CMake |

The Makefile finds sources with a wildcard and generates header dependencies (`-MMD`), so a new file in `src/` is
picked up automatically and editing a header rebuilds only what includes it.

## Run

```bash
./build/alpshell                # interactive (stdin is a terminal)
./build/alpshell script.sh      # run a file
./build/alpshell -c 'ls | wc -l'
echo 'ls | wc -l' | ./build/alpshell
```

## Source layout

| File | Role |
|---|---|
| `main.cpp` | option handling, the readline loop, continuation lines, Ctrl+C at the prompt |
| `src/lexer.cpp` | turns a line into words (each remembering how its parts were quoted) and operators |
| `src/parser.cpp` | builds command lists, pipelines and redirections from tokens, or reports a syntax error |
| `src/expand.cpp` | `$VAR`, `~`, field splitting and globbing, after the quoting rules |
| `src/variables.cpp` | shell variables and the exported environment |
| `src/shell.cpp` | the interpreter: runs lists, expands commands, keeps `$?`, decides what runs in the shell itself |
| `src/builtins.cpp` | `cd pwd echo export unset exit history wait jobs fg bg kill` |
| `src/executor.cpp` | forks the processes of a pipeline, wires up pipes and redirections |
| `src/job_control.cpp` | job table, process groups, terminal hand-off, waiting and job notices |
| `src/history.cpp` | readline history, loaded from and saved to a file |
| `src/prompt.cpp`, `src/syntax_highlight.cpp`, `src/completion.cpp` | the prompt, the red `$`, Tab completion |

## Tests

| File | What it checks |
|---|---|
| `tests/unit_tests.cpp` | lexer, parser, expansion and variables (a tiny built-in test runner) |
| `tests/script_cases.txt`, `tests/run_script_tests.py` | scripts run under `bash` and `alpshell` must agree on stdout, stderr and exit status; intentional differences are declared with a reason |
| `tests/cli_test.py` | `-c`, script files, commands on stdin, exit codes and usage errors |
| `tests/pty_tests.py` | interactive behaviour on a pseudo-terminal: Ctrl+C, Ctrl+Z, `fg`, `bg`, `kill`, notices, history, continuation lines, Tab completion, the red `$`, exit codes |
| `tests/fork_failure_test.py`, `tests/failfork.c` | one `fork` or `pipe` call fails (via `LD_PRELOAD`); the processes already started must be killed and reaped |

Run one test file directly, for example `python3 tests/pty_tests.py --shell build/alpshell`, or only matching
script cases with `python3 tests/run_script_tests.py --shell build/alpshell --filter pipe -v`.
