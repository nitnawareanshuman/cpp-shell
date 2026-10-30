# Architecture

`Shell::execute(line, options)` is the only entry point for running commands.
Both `src/main.cpp` (CLI) and `src/ui/dashboard.cpp` (FTXUI) call it. The dashboard does not run commands itself.

Pipeline of one command line:

1. `lexer` splits text into tokens (words keep their quote kind).
2. `parser` builds a pipeline of simple commands with redirections.
3. `expander` expands `$NAME`, `${NAME}`, `$?`, `~` and splits fields (quote aware).
4. `executor` runs built-ins in the shell process, or forks children for external commands and pipelines.
5. `JobTable` tracks background jobs and reaps them.

Other parts: `History` (memory and file), `Completer` (commands, paths, JSON rules), `LineEditor` (terminal input).
Capture mode (`ExecOptions::capture_output`) returns output as strings. The dashboard uses it.

POSIX-specific code is in `src/core/executor.cpp`, `src/core/line_editor.cpp` and `src/jobs/jobs.cpp`.
