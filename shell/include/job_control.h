#ifndef JOB_CONTROL_H
#define JOB_CONTROL_H

#include <sys/types.h>
#include <termios.h>

#include <list>
#include <string>
#include <vector>

// Job control as in a POSIX shell: every pipeline is a job, a job is a process group, the shell gives the
// terminal to the foreground job and takes it back when that job finishes or stops.
//
// Job control is only active when the shell is interactive (stdin is a terminal). In scripts the shell does
// not touch process groups or the terminal, exactly like bash, so Ctrl+C still reaches everything.
//
// Nothing here reaps children from a signal handler: statuses are collected with waitpid on the specific
// process group (or pid) of a job, either blocking (foreground) or WNOHANG (background), so the status of a
// foreground command cannot be stolen by a background reaper.

enum class JobState { Running, Stopped, Done };

struct Process {
    pid_t pid = 0;
    bool finished = false;
    bool stopped = false;
    int wait_status = 0;  // raw status from waitpid, valid once finished
};

struct Job {
    int id = 0;            // 0 until the job is stopped or put in the background
    pid_t pgid = 0;        // 0 when job control is off (scripts)
    std::vector<Process> procs;
    std::string command;   // text shown by `jobs`
    bool foreground = false;
    JobState reported = JobState::Running;  // last state the user was told about
    bool has_tmodes = false;
    struct termios tmodes{};  // terminal modes saved when the job stopped

    JobState state() const;
    bool all_finished() const;
    // Shell-style exit status of the job: that of the last process, 128+signal if it was killed or stopped.
    int exit_status() const;
    // Records a waitpid result for `pid`. Returns false if the pid does not belong to the job.
    bool update(pid_t pid, int wait_status);
};

namespace JobControl {

// Claims the terminal and ignores SIGINT, SIGQUIT, SIGTSTP, SIGTTIN and SIGTTOU for the shell itself. Does
// nothing (job control stays off) if `interactive` is false or stdin is not a terminal.
void init(bool interactive);
bool enabled();
int terminal_fd();

// Signal setup for a freshly forked child: default dispositions, and for background jobs of a
// non-interactive shell, SIGINT and SIGQUIT ignored (as POSIX requires for asynchronous commands).
void reset_signals_in_child(bool ignore_int_and_quit);

// Creates a job for already-forked processes. `pgid` is 0 when processes are not in their own group.
Job* add_job(pid_t pgid, const std::vector<pid_t>& pids, const std::string& command, bool foreground);

// Blocks until the job has finished or stopped, then returns the terminal to the shell. A finished
// foreground job is removed from the table (the pointer is then invalid); a stopped one gets a job id and
// stays (*stopped is set to true). Returns the job's exit status.
int wait_foreground(Job* job, bool* stopped = nullptr);
// Gives the terminal to `job` (continuing it first if `cont`), and waits for it like wait_foreground.
int put_in_foreground(Job* job, bool cont, bool* stopped = nullptr);
// Lets the job run without the terminal; sends SIGCONT first if `cont`.
void put_in_background(Job* job, bool cont);

// Collects the state changes of all background jobs without blocking.
void reap();
// Blocks until the job has finished (used by `wait`). Does not report stops.
int wait_for(Job* job);
// Prints "Done" / "Stopped" notices for jobs whose state changed since the last notice, and drops the
// finished ones. Called before each prompt. Silent unless job control is on.
void notify();

// Output of the `jobs` builtin (also shows jobs that finished since the last notice, then drops them).
void print_jobs();

Job* find_by_id(int id);
Job* find_by_pid(pid_t pid);
Job* current_job();  // the most recently created job that is not finished, or nullptr
// Parses "%n", "%%", "%+" or "n" into a job; nullptr if there is no such job.
Job* find_by_spec(const std::string& spec);
bool has_jobs();
// Sends `sig` to every process of the job (the whole group when there is one).
int signal_job(Job* job, int sig);
// Kills and reaps already-forked processes of a pipeline that could not be fully started.
void abort_launch(pid_t pgid, const std::vector<pid_t>& pids);

}  // namespace JobControl

#endif // JOB_CONTROL_H
