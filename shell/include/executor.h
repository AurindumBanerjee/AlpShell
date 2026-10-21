#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <sys/types.h>

#include <string>
#include <vector>

#include "command.h"

enum class RedirOp {
    In,         // < path
    Out,        // > path
    Append,     // >> path
    ErrOut,     // 2> path
    ErrAppend,  // 2>> path
    ErrToOut,   // 2>&1
    OutToErr    // >&2
};

struct ResolvedRedir {
    RedirOp op = RedirOp::Out;
    std::string path;  // unused for ErrToOut and OutToErr
};

// One command of a pipeline, ready to run: arguments are already expanded, redirections are applied in order.
struct ProcessSpec {
    std::vector<std::string> argv;
    std::vector<ResolvedRedir> redirs;
};

struct PipelineResult {
    int status = 0;         // exit status of the pipeline; 128 + signal if it was killed or stopped
    bool launched = true;   // false when fork or pipe failed: nothing is left running
    bool stopped = false;   // the foreground job was stopped (Ctrl+Z); it stays in the job table
    pid_t last_pid = 0;     // pid of the last process, for background jobs
};

// Starts the processes of a pipeline connected by pipes. Each pipeline gets its own process group when job
// control is on. A foreground pipeline gets the terminal and is waited for; a background one is only
// recorded in the job table (with `text` as its description).
PipelineResult runPipeline(const std::vector<ProcessSpec>& procs, bool background, const std::string& text);

// Opens files and rewires stdin/stdout/stderr as the redirections say. Prints a message and returns false
// if a file cannot be opened.
bool applyRedirections(const std::vector<ResolvedRedir>& redirs);

// Runs a parsed pipeline of Command objects (converts them to ProcessSpecs).
PipelineResult executePipeline(const std::vector<Command>& commands, bool background, const std::string& text);

#endif // EXECUTOR_H
