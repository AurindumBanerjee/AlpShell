# shell/

The AlpShell source. See the [top-level README](../README.md) for what the shell can and cannot do.

## Build

Requires a C++17 compiler and GNU readline with its headers (`libreadline-dev` on Debian/Ubuntu). Linux only: the shell
uses `fork`, `exec`, `termios` and `glob(3)`.

| Command | Result |
|---|---|
| `make` | `build/alpshell`, built with `-std=c++17 -Wall -Wextra` |
| `make asan` | `build-asan/alpshell`, built with AddressSanitizer and UBSan |
| `make clean` | removes `build/` and `build-asan/` |
| `cmake -S . -B build-cmake && cmake --build build-cmake` | the same program through CMake |
| `cmake -S . -B build-cmake -DALPSHELL_SANITIZE=ON` | CMake build with the sanitizers |

The Makefile finds sources with a wildcard and generates header dependencies (`-MMD`), so a new file in `src/` is
picked up automatically and editing a header rebuilds only what includes it.

## Run

```bash
./build/alpshell                # interactive (stdin is a terminal)
./build/alpshell script.sh      # run a file
echo 'ls | wc -l' | ./build/alpshell
```

## Source layout

| File | Role |
|---|---|
| `main.cpp` | startup, readline loop, line processing, completion |
| `src/lexer.cpp` | splits a line into word, pipe and redirection tokens, tracking quoting |
| `src/parser.cpp` | builds commands from tokens and expands variables and globs |
| `src/executor.cpp` | forks and wires up pipelines |
| `src/job_control.cpp` | background job table and the `SIGCHLD` handler |
| `src/history.cpp` | in-memory history list used by the `history` builtin |
| `src/prompt.cpp` | colored prompt |
| `src/syntax_highlight.cpp` | marks the prompt red when the line has a syntax problem |
| `src/shell.cpp`, `src/util.cpp` | unused stubs |
