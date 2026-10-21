#!/usr/bin/env python3
"""Interactive job-control tests: the shell runs on a pseudo-terminal, so Ctrl+C and Ctrl+Z are real
terminal signals. Besides the text on the screen, the tests look at the real process state in /proc.

    python3 tests/pty_tests.py --shell build/alpshell
"""
import argparse
import os
import pty
import re
import select
import signal
import subprocess
import sys
import tempfile
import time
import unittest

SHELL = "build/alpshell"
CTRL_C = "\x03"
CTRL_Z = "\x1a"


class Terminal:
    """The shell under test, attached to a pty."""

    def __init__(self, home, path="/usr/local/bin:/usr/bin:/bin"):
        # The environment is fixed, so the tests do not depend on the machine (under WSL, for example, PATH also
        # lists the Windows directories, and CI runners have extra interpreters on PATH).
        self.path = path
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.update(HOME=home, TERM="xterm", LC_ALL="C", PATH=path,
                              ALPSHELL_HISTFILE=os.path.join(home, "history"))
            os.execv(SHELL, [SHELL])
        self.buf = ""
        self.mark = 0
        self.status = None  # exit status of the shell once it has been reaped

    def send(self, text):
        os.write(self.fd, text.encode())

    def line(self, text):
        self.send(text + "\n")

    def expect(self, regex, timeout=10.0):
        """Waits until `regex` matches the output received since the last successful expect."""
        deadline = time.time() + timeout
        pattern = re.compile(regex)
        while True:
            m = pattern.search(self.buf, self.mark)
            if m:
                self.mark = m.end()
                return m
            left = deadline - time.time()
            if left <= 0:
                raise AssertionError("timed out waiting for %r\nPATH=%s\nunread output: %r\nall output: %r"
                                     % (regex, self.path, self.buf[self.mark:][-400:], self.buf[-2000:]))
            ready, _, _ = select.select([self.fd], [], [], min(left, 0.2))
            if ready:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:
                    data = b""
                if not data:
                    raise AssertionError("shell closed the terminal while waiting for %r\nPATH=%s\nall output: %r"
                                         % (regex, self.path, self.buf[-2000:]))
                self.buf += data.decode("utf-8", "replace")

    def echoes(self, word, timeout=10.0):
        """Runs `echo word` and waits for its output (a line by itself), which proves the shell is alive."""
        self.line("echo " + word)
        return self.expect(r"\r\n" + re.escape(word) + r"\r\n", timeout)

    def alive(self):
        if self.status is not None:
            return False
        done, status = os.waitpid(self.pid, os.WNOHANG)
        if done != 0:
            self.status = status
        return done == 0

    def wait_exit(self, timeout=5.0):
        """Waits for the shell to exit and returns its exit code."""
        deadline = time.time() + timeout
        while self.alive():
            if time.time() > deadline:
                raise AssertionError("the shell did not exit")
            time.sleep(0.05)
        return os.waitstatus_to_exitcode(self.status)

    def close(self):
        if self.alive():
            try:
                self.line("exit")
            except OSError:
                pass
            for _ in range(30):
                if not self.alive():
                    break
                time.sleep(0.1)
            else:
                os.kill(self.pid, signal.SIGKILL)
                os.waitpid(self.pid, 0)
        os.close(self.fd)


def pids(pattern):
    out = subprocess.run(["pgrep", "-f", pattern], capture_output=True, text=True).stdout.split()
    return [int(p) for p in out]


def proc_state(pid):
    """One-letter process state from /proc (R, S, T = stopped, Z = zombie), or None if the process is gone."""
    try:
        with open("/proc/%d/stat" % pid) as f:
            return f.read().rsplit(")", 1)[1].split()[0]
    except FileNotFoundError:
        return None


def wait_state(pid, wanted, timeout=4.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc_state(pid) in wanted:
            return True
        time.sleep(0.05)
    return False


def wait_gone(pattern, timeout=4.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not pids(pattern):
            return True
        time.sleep(0.05)
    return False


class JobControlTests(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.TemporaryDirectory()
        self.term = Terminal(self.home.name)
        self.term.expect(r"\$ ")
        self.patterns = []

    def tearDown(self):
        self.term.close()
        for p in self.patterns:
            subprocess.run(["pkill", "-9", "-f", p])
        self.home.cleanup()

    def sleeper(self, tag):
        pattern = "sleep " + tag
        self.patterns.append(pattern)
        return pattern

    def test_ctrl_c_kills_the_foreground_command_but_not_the_shell(self):
        pattern = self.sleeper("7701")
        self.term.line(pattern)
        time.sleep(0.4)
        self.assertTrue(pids(pattern), "sleep should be running")
        self.term.send(CTRL_C)
        self.assertTrue(wait_gone(pattern), "Ctrl+C should kill the foreground sleep")
        self.term.echoes("STILL_ALIVE_1")
        self.assertTrue(self.term.alive())

    def test_ctrl_c_kills_every_process_of_a_pipeline(self):
        pattern = self.sleeper("7702")
        self.term.line(pattern + " | cat")
        time.sleep(0.4)
        self.assertTrue(pids(pattern))
        self.term.send(CTRL_C)
        self.assertTrue(wait_gone(pattern), "the sleep side of the pipeline should die")
        self.term.echoes("STILL_ALIVE_2")
        self.assertEqual(pids(r"^cat$"), [], "the cat side of the pipeline should die as well")

    def test_ctrl_z_stops_the_job_and_bg_fg_resume_it(self):
        pattern = self.sleeper("7703")
        self.term.line(pattern)
        time.sleep(0.4)
        (pid,) = pids(pattern)
        self.term.send(CTRL_Z)
        self.term.expect(r"\[1\]\+\s+Stopped\s+" + pattern)
        self.assertTrue(wait_state(pid, "T"), "the process should really be stopped")
        self.term.line("jobs")
        self.term.expect(r"\[1\]\+\s+Stopped\s+" + pattern)

        self.term.line("bg")
        self.term.expect(r"\[1\]\+ " + pattern + r" &")
        self.assertTrue(wait_state(pid, "SR"), "bg should continue the process")
        self.term.line("jobs")
        self.term.expect(r"\[1\]\+\s+Running\s+" + pattern + r" &")
        self.term.echoes("SHELL_FREE_WHILE_BG")  # the shell is usable while the job runs

        self.term.line("fg")
        self.term.expect(r"\r\n" + pattern + r"\r\n")  # fg prints the command it resumes
        time.sleep(0.3)
        self.term.send(CTRL_C)  # the resumed job owns the terminal now: Ctrl+C reaches it, not the shell
        self.assertTrue(wait_gone(pattern))
        self.term.echoes("STILL_ALIVE_3")

    def test_stopped_job_can_be_stopped_again_after_fg(self):
        pattern = self.sleeper("7704")
        self.term.line(pattern)
        time.sleep(0.4)
        (pid,) = pids(pattern)
        self.term.send(CTRL_Z)
        self.term.expect(r"\[1\]\+\s+Stopped")
        self.term.line("fg")
        self.term.expect(r"\r\n" + pattern + r"\r\n")
        time.sleep(0.3)
        self.term.send(CTRL_Z)
        self.term.expect(r"\[1\]\+\s+Stopped\s+" + pattern)
        self.assertTrue(wait_state(pid, "T"))
        self.term.line("kill %1")
        self.assertTrue(wait_gone(pattern), "kill must also wake and kill a stopped job")

    def test_finished_background_job_is_announced_before_the_next_prompt(self):
        pattern = self.sleeper("0.3")
        self.term.line(pattern + " &")
        self.term.expect(r"\[1\] \d+")
        time.sleep(0.8)
        self.term.line("")  # an empty line just brings up the next prompt
        self.term.expect(r"\[1\]\+\s+Done\s+" + re.escape(pattern))
        self.term.line("jobs")
        self.term.echoes("NO_JOBS_LEFT")  # the finished job is gone from the table

    def test_kill_by_job_id_terminates_a_background_job(self):
        pattern = self.sleeper("7705")
        self.term.line(pattern + " &")
        self.term.expect(r"\[1\] \d+")
        self.term.line("kill %1")
        self.assertTrue(wait_gone(pattern))
        time.sleep(0.2)
        self.term.line("")
        self.term.expect(r"\[1\]\+\s+Terminated\s+" + pattern)

    def test_background_job_that_reads_the_terminal_is_stopped_not_the_shell(self):
        self.term.line("cat &")
        self.term.expect(r"\[1\] \d+")
        time.sleep(0.5)
        self.term.line("jobs")
        self.term.expect(r"\[1\]\+\s+Stopped")  # SIGTTIN stopped it
        self.term.line("kill %1")
        self.term.line("")
        self.term.echoes("STILL_ALIVE_4")
        self.assertEqual(pids(r"^cat$"), [])

    def test_unknown_job_is_reported(self):
        self.term.line("fg %9")
        self.term.expect(r"fg: %9: no such job")
        self.term.line("kill %9")
        self.term.expect(r"kill: %9: no such job")
        self.term.echoes("STILL_ALIVE_5")

    def test_ctrl_c_at_the_prompt_discards_the_line(self):
        self.term.send("echo should_not_run")
        self.term.send(CTRL_C)
        time.sleep(0.2)
        self.term.echoes("PROMPT_STILL_WORKS")
        self.assertNotIn("\r\nshould_not_run\r\n", self.term.buf)


class StatusAndSessionTests(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.home.cleanup()

    def start(self, path="/usr/local/bin:/usr/bin:/bin"):
        term = Terminal(self.home.name, path)
        term.expect(r"\$ ")
        return term

    def test_status_after_ctrl_c_is_130_and_after_ctrl_z_is_148(self):
        term = self.start()
        try:
            term.line("sleep 7711")
            time.sleep(0.4)
            term.send(CTRL_C)
            term.expect(r"\$ ")
            term.line("echo STATUS=$?")
            term.expect(r"\r\nSTATUS=130\r\n")
            term.line("sleep 7712")
            time.sleep(0.4)
            term.send(CTRL_Z)
            term.expect(r"Stopped")
            term.line("echo STATUS=$?")
            term.expect(r"\r\nSTATUS=148\r\n")
            term.line("kill %1")
            term.echoes("STILL_ALIVE_6")
        finally:
            term.close()
            subprocess.run(["pkill", "-9", "-f", "sleep 771"])

    def test_the_shell_exits_with_a_requested_status(self):
        term = self.start()
        term.line("exit 7")
        self.assertEqual(term.wait_exit(), 7)
        os.close(term.fd)

    def test_the_shell_exits_with_the_status_of_the_last_command(self):
        term = self.start()
        term.line("false")
        term.line("exit")
        self.assertEqual(term.wait_exit(), 1)
        os.close(term.fd)

    def test_ctrl_d_at_an_empty_prompt_exits(self):
        term = self.start()
        term.send("\x04")
        term.expect(r"exit")
        self.assertEqual(term.wait_exit(), 0)
        os.close(term.fd)

    def test_history_is_saved_and_loaded_through_readlines_store(self):
        first = self.start()
        first.echoes("HIST_ONE")
        first.line("echo HIST_TWO")
        first.expect(r"\r\nHIST_TWO\r\n")
        first.close()
        with open(os.path.join(self.home.name, "history")) as f:
            saved = f.read()
        self.assertIn("echo HIST_ONE", saved)
        self.assertIn("echo HIST_TWO", saved)

        second = self.start()
        try:
            second.line("history")
            second.expect(r"echo HIST_ONE")
            second.expect(r"echo HIST_TWO")
            # The up arrow walks back through the history: first this session's own `history`, then the
            # entries that were loaded from the file ("exit", then "echo HIST_TWO").
            for _ in range(3):
                second.send("\x1b[A")
                time.sleep(0.15)
            second.send("\n")  # run the recalled command; readline only redraws changed characters on screen
            second.expect(r"\r\nHIST_TWO\r\n")
        finally:
            second.close()

    def test_a_command_can_continue_on_the_next_line(self):
        term = self.start()
        try:
            term.line('echo "first')
            term.expect(r"\r\n> ")  # the continuation prompt
            term.line('second"')
            term.expect(r"\r\nfirst\r\nsecond\r\n")
            term.line("echo one |")
            term.expect(r"\r\n> ")
            term.line("cat")
            term.expect(r"\r\none\r\n")
        finally:
            term.close()

    def test_ctrl_c_cancels_a_command_that_was_being_continued(self):
        term = self.start()
        try:
            term.line('echo "never printed')
            term.expect(r"\r\n> ")
            term.send(CTRL_C)
            time.sleep(0.2)
            term.echoes("BACK_TO_NORMAL")
            self.assertNotIn("never printed\r\n", term.buf[term.buf.rfind("echo \"never") + 20:])
        finally:
            term.close()

    def commands_dir(self):
        """A directory of fake executables, so completion depends on nothing installed on the machine."""
        bindir = os.path.join(self.home.name, "bin")
        os.mkdir(bindir)
        for name in ("zzcmd_alpha_one", "zzcmd_alpha_two", "zzcmd_unique_only"):
            path = os.path.join(bindir, name)
            with open(path, "w") as f:
                f.write("#!/bin/sh\n")
            os.chmod(path, 0o755)
        with open(os.path.join(bindir, "zzcmd_not_executable"), "w") as f:
            f.write("data\n")
        return bindir

    def test_tab_completes_builtins_and_commands_in_command_position(self):
        term = self.start(self.commands_dir())  # PATH holds only the fake commands
        try:
            term.send("unse\t")  # `unset` is only a builtin: a unique completion
            term.expect(r"unset ")
            term.send(CTRL_C)
            term.expect(r"\$ ")

            term.send("zzcmd_uni\t")  # a unique command on PATH: completed, with a trailing space
            term.expect(r"zzcmd_uni\x07?que_only ")
            term.send(CTRL_C)
            term.expect(r"\$ ")

            # Two candidates: only the common prefix is inserted. The next character typed lands right after it, which
            # shows that nothing further was completed. (Readline rings the bell only when no progress is possible, so
            # the test does not look at it.)
            term.send("zzcmd_alp\t")
            term.send("Q")
            term.expect(r"zzcmd_alp\x07?ha_Q")
            term.send(CTRL_C)
            term.expect(r"\$ ")

            term.send("zzcmd_not\t")  # a file without the execute bit is not a command
            term.send("Q")
            term.expect(r"zzcmd_not\x07?Q")
            term.send(CTRL_C)
            term.expect(r"\$ ")
            term.echoes("STILL_ALIVE_8")
        finally:
            term.close()

    def test_tab_completes_file_names_in_argument_position(self):
        open(os.path.join(self.home.name, "unique_file_name_xyz"), "w").close()
        term = self.start()
        try:
            term.line("cd " + self.home.name)
            term.expect(r"\$ ")
            term.send("echo unique_file_n\t")
            term.expect(r"ame_xyz ")
        finally:
            term.close()

    def test_the_prompt_dollar_turns_red_while_the_line_does_not_parse(self):
        term = self.start()
        try:
            term.send("echo a |")  # incomplete: ends in an operator
            term.expect(r"\x1b\[31m\x1b\[1m\$ ")
            term.send(" cat")  # now it parses again: the $ goes back to green
            term.expect(r"\x1b\[32m\x1b\[1m\$ ")
            term.send(CTRL_C)
            time.sleep(0.2)
            term.echoes("STILL_ALIVE_9")
        finally:
            term.close()

    def test_a_syntax_error_does_not_end_an_interactive_shell(self):
        term = self.start()
        try:
            term.line("echo a | | echo b")
            term.expect(r"syntax error near unexpected token `\|'")
            term.line("echo STATUS=$?")
            term.expect(r"\r\nSTATUS=2\r\n")
            term.echoes("STILL_ALIVE_7")
        finally:
            term.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--shell", default=SHELL)
    args, rest = parser.parse_known_args()
    SHELL = os.path.abspath(args.shell)
    unittest.main(argv=[sys.argv[0]] + rest, verbosity=2)
