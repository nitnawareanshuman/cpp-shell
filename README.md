# cpp-shell

A small Unix shell written in C++23, with an optional desktop dashboard built with [Qt](https://www.qt.io/).
The CLI and the dashboard use the same shell engine.

> Status: work in progress. Only features listed under "Features" are implemented and covered by tests.
> This is not a complete POSIX shell.

## Features

- Built-ins: `echo`, `pwd`, `cd`, `type`, `exit`, `export`, `history`, `jobs`, `fg`, `bg`, `help`
- Quoting: single quotes, double quotes, backslash escapes
- Redirection: `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`
- Pipelines: `a | b | c` (exit status is the status of the last command)
- Expansion: `$NAME`, `${NAME}`, `$?`, `~`, `~/path`
- Background jobs: `cmd &`, `jobs`, `fg`, `bg`, `help`
- History: Up/Down in the editor, saved to a file
- Tab completion: built-ins, `PATH` commands, files and directories (spaces are quoted)
- Programmable completion from a JSON file
- Qt dashboard: prompt, output, working directory, jobs

## Build

Requirements: CMake 3.20+, a C++23 compiler, Qt 6 Widgets (via vcpkg).

```sh
git clone https://github.com/nitnawareanshuman/cpp-shell
cd cpp-shell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

If Qt is installed elsewhere, use `-DCMAKE_PREFIX_PATH=/path/to/qt6` instead of the toolchain file.

## Run

```sh
./build/cpp-shell              # interactive CLI
./build/cpp-shell --ui         # Qt dashboard
./build/cpp-shell -c 'echo hi | cat'   # run one command
```

## Examples

```sh
echo "hello $USER" > out.txt
echo more >> out.txt
ls /missing 2> err.txt
ls /missing > all.txt 2>&1
cat < out.txt | sort | head -n 3
sleep 5 &
jobs
fg 1
```

## History

Default file: `~/.cpp-shell_history`. Set `CPP_SHELL_HISTFILE` to use another file.
Existing entries are kept. New commands are appended. Blank commands are not saved.

## Programmable completion

Rules live in a JSON file (see `examples/completions.json`). Rules are data only. No scripts are run.

```json
{
  "git": {
    "args": { "1": ["add", "commit", "status"] },
    "files": true
  },
  "cd": { "directories_only": true }
}
```

- `args`: candidates by argument position (`"*"` means any position)
- `files`: fall back to file names
- `directories_only`: only directories

A malformed file is reported as an error and ignored.

## Tests

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests use temporary directories. `shell_unit_tests` covers the lexer, parser, expansion, history and completion.
`shell_integration_tests` runs real commands: redirection, pipelines (including large input), background jobs, history, completion and the CLI binary.

## Layout

```
include/shell/   public headers
src/core/        lexer, parser, expansion, executor, line editor
src/jobs/        job table
src/history/     history and persistence
src/completion/  completion
src/ui/          Qt dashboard
tests/           unit and integration tests
docs/            architecture notes
```

## Platform and limits

- Target: macOS on Apple Silicon. The code uses POSIX calls (`fork`, `exec`, `pipe`, `tcsetpgrp`), so it should also build on Linux.
- Not implemented: `&&`, `||`, `;`, globbing, here-documents, scripts, command substitution, `$(...)`.
- The dashboard needs a real terminal. It has no automated test yet. Interactive `fg`/`bg` with Ctrl-Z needs a real terminal and has only been tested without one.

## Future work

- Automated terminal tests for the dashboard and Ctrl-Z job control
- `&&`, `||`, `;` and globbing
- Workflow management in the dashboard
