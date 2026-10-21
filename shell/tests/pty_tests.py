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

    def __init__(self, home):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.update(HOME=home, TERM="xterm", LC_ALL="C", ALPSHELL_HISTFILE=os.path.join(home, "history"))
            os.execv(SHELL, [SHELL])
        self.buf = ""
        self.mark = 0

    def send(self, text):
        os.write(self.fd, text.encode())

    def line(self, text):
        self.send(text + "\n")

    def expect(self, regex, timeout=6.0):
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
                tail = self.buf[self.mark:][-400:]
                raise AssertionError("timed out waiting for %r; recent output: %r" % (regex, tail))
            ready, _, _ = select.select([self.fd], [], [], min(left, 0.2))
            if ready:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:
                    data = b""
                if not data:
                    raise AssertionError("shell closed the terminal while waiting for %r" % regex)
                self.buf += data.decode("utf-8", "replace")

    def echoes(self, word, timeout=6.0):
        """Runs `echo word` and waits for its output (a line by itself), which proves the shell is alive."""
        self.line("echo " + word)
        return self.expect(r"\r\n" + re.escape(word) + r"\r\n", timeout)

    def alive(self):
        done, _ = os.waitpid(self.pid, os.WNOHANG)
        return done == 0

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


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--shell", default=SHELL)
    args, rest = parser.parse_known_args()
    SHELL = os.path.abspath(args.shell)
    unittest.main(argv=[sys.argv[0]] + rest, verbosity=2)
