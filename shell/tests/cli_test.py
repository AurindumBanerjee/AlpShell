#!/usr/bin/env python3
"""Command-line behaviour: -c, a script file, commands on stdin, and usage errors.

    python3 tests/cli_test.py --shell build/alpshell
"""
import argparse
import os
import subprocess
import sys
import tempfile
import unittest

SHELL = "build/alpshell"


def run(args, stdin=None):
    env = {"PATH": "/usr/local/bin:/usr/bin:/bin", "HOME": "/usr", "LC_ALL": "C"}
    return subprocess.run([SHELL] + args, input=stdin, capture_output=True, text=True, env=env, timeout=20)


class CliTests(unittest.TestCase):
    def test_dash_c_runs_a_command_string(self):
        r = run(["-c", "echo hi; echo there"])
        self.assertEqual((r.stdout, r.returncode), ("hi\nthere\n", 0))

    def test_dash_c_exit_status_is_the_status_of_the_last_command(self):
        self.assertEqual(run(["-c", "false"]).returncode, 1)
        self.assertEqual(run(["-c", "exit 5"]).returncode, 5)
        self.assertEqual(run(["-c", "nosuchcmd_xyz"]).returncode, 127)
        self.assertEqual(run(["-c", "true"]).returncode, 0)

    def test_dash_c_may_contain_several_lines(self):
        r = run(["-c", "echo a\necho b\n"])
        self.assertEqual(r.stdout, "a\nb\n")

    def test_stdin_is_run_as_a_script_and_exit_stops_it(self):
        r = run([], stdin="echo from_stdin\nexit 3\necho never\n")
        self.assertEqual((r.stdout, r.returncode), ("from_stdin\n", 3))

    def test_a_script_file_is_run(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "s.sh")
            with open(path, "w") as f:
                f.write("# comment\necho in_file\nfalse\n")
            r = run([path])
            self.assertEqual((r.stdout, r.returncode), ("in_file\n", 1))

    def test_a_missing_script_file_is_an_error(self):
        r = run(["/nonexistent/script.sh"])
        self.assertEqual(r.returncode, 127)
        self.assertIn("No such file or directory", r.stderr)

    def test_usage_errors_exit_with_2(self):
        for args in (["-c"], ["-x"], ["a", "b"], ["-c", "true", "file"]):
            with self.subTest(args=args):
                r = run(args)
                self.assertEqual(r.returncode, 2)
                self.assertIn("usage", r.stderr)

    def test_help(self):
        r = run(["--help"])
        self.assertEqual(r.returncode, 0)
        self.assertIn("usage", r.stdout)

    def test_input_that_ends_inside_a_quote_is_a_syntax_error(self):
        r = run(["-c", 'echo "abc'])
        self.assertEqual(r.returncode, 2)
        self.assertIn("unexpected end of file", r.stderr)
        self.assertEqual(r.stdout, "")

    def test_a_syntax_error_stops_a_script_but_earlier_output_stays(self):
        r = run(["-c", "echo before\necho a | | b\necho after\n"])
        self.assertEqual((r.stdout, r.returncode), ("before\n", 2))
        self.assertIn("syntax error near unexpected token `|'", r.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--shell", default=SHELL)
    args, rest = parser.parse_known_args()
    SHELL = os.path.abspath(args.shell)
    unittest.main(argv=[sys.argv[0]] + rest, verbosity=2)
