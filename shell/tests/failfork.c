/* Test helper, loaded with LD_PRELOAD: makes fork() or pipe2() fail on demand so the shell's cleanup of a
 * half-started pipeline can be tested deterministically.
 *
 *   FAILFORK_AT=N   the N-th fork() call (counting from 0) fails with EAGAIN; all other calls succeed
 *   FAILPIPE_AT=N   the N-th pipe2() call (counting from 0) fails with EMFILE; all other calls succeed
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static int limit_from_env(const char* name) {
    const char* v = getenv(name);
    return v ? atoi(v) : -1;
}

pid_t fork(void) {
    static pid_t (*real_fork)(void);
    static int calls;
    if (!real_fork) real_fork = (pid_t(*)(void))dlsym(RTLD_NEXT, "fork");
    const int target = limit_from_env("FAILFORK_AT");
    if (target >= 0 && calls++ == target) {
        errno = EAGAIN;
        return -1;
    }
    return real_fork();
}

int pipe2(int fds[2], int flags) {
    static int (*real_pipe2)(int[2], int);
    static int calls;
    if (!real_pipe2) real_pipe2 = (int (*)(int[2], int))dlsym(RTLD_NEXT, "pipe2");
    const int target = limit_from_env("FAILPIPE_AT");
    if (target >= 0 && calls++ == target) {
        errno = EMFILE;
        return -1;
    }
    return real_pipe2(fds, flags);
}
