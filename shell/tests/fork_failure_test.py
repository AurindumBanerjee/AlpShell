#!/usr/bin/env python3
"""A pipeline whose fork() or pipe() fails halfway must not leave the already-started processes running.

    python3 tests/fork_failure_test.py --shell build/alpshell

Uses tests/failfork.c (compiled here with the system C compiler) to make exactly one fork or pipe call fail.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time
import unittest

SHELL = "build/alpshell"
HERE = os.path.dirname(os.path.abspath(__file__))
SHIM = None


def running(pattern):
    return subprocess.run(["pgrep", "-f", pattern], capture_output=True, text=True).stdout.split()


def run_shell(script, **env):
    full_env = dict(os.environ, LD_PRELOAD=SHIM, ASAN_OPTIONS="verify_asan_link_order=0:detect_leaks=0", LC_ALL="C")
    full_env.update(env)
    return subprocess.run([SHELL], input=script, capture_output=True, text=True, env=full_env, timeout=20)


class ForkFailureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        global SHIM
        cls.tmp = tempfile.TemporaryDirectory()
        SHIM = os.path.join(cls.tmp.name, "failfork.so")
        subprocess.run(["cc", "-shared", "-fPIC", "-O1", "-o", SHIM, os.path.join(HERE, "failfork.c"), "-ldl"], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def tearDown(self):
        subprocess.run(["pkill", "-9", "-f", "sleep 770"])

    def test_fork_failure_in_the_middle_kills_and_reaps_the_started_children(self):
        out = run_shell("sleep 7701 | sleep 7701 | sleep 7701\necho AFTER_FORK_FAILURE\n", FAILFORK_AT="1")
        self.assertIn("alpshell: fork: Resource temporarily unavailable", out.stderr)
        self.assertIn("AFTER_FORK_FAILURE", out.stdout, "the shell must keep going after the failure")
        time.sleep(0.3)
        self.assertEqual(running("sleep 7701"), [], "the first sleep, started before the failure, must be gone")

    def test_pipe_failure_in_the_middle_kills_and_reaps_the_started_children(self):
        out = run_shell("sleep 7702 | sleep 7702 | sleep 7702\necho AFTER_PIPE_FAILURE\n", FAILPIPE_AT="1")
        self.assertIn("alpshell: pipe: Too many open files", out.stderr)
        self.assertIn("AFTER_PIPE_FAILURE", out.stdout)
        time.sleep(0.3)
        self.assertEqual(running("sleep 7702"), [])

    def test_first_fork_failing_starts_nothing(self):
        out = run_shell("sleep 7703 | sleep 7703\necho AFTER_FIRST\n", FAILFORK_AT="0")
        self.assertIn("fork: Resource temporarily unavailable", out.stderr)
        self.assertIn("AFTER_FIRST", out.stdout)
        self.assertEqual(running("sleep 7703"), [])

    def test_a_single_command_that_cannot_fork_is_reported(self):
        out = run_shell("sleep 7704\necho AFTER_SINGLE\n", FAILFORK_AT="0")
        self.assertIn("fork: Resource temporarily unavailable", out.stderr)
        self.assertIn("AFTER_SINGLE", out.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--shell", default=SHELL)
    args, rest = parser.parse_known_args()
    SHELL = os.path.abspath(args.shell)
    unittest.main(argv=[sys.argv[0]] + rest, verbosity=2)
