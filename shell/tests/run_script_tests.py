#!/usr/bin/env python3
"""Runs each case in tests/script_cases.txt with bash and with alpshell, non-interactively, and compares
stdout, stderr and the exit status.

    python3 tests/run_script_tests.py --shell build/alpshell [--bash bash] [--filter pipe] [-v]

Case file format (see the top of script_cases.txt):

    === case_name
    #! compare: stdout status        which of stdout / stderr / status must match bash (default: all three)
    #! diff: reason                  an intentional difference from bash; the case then checks alpshell's own output
    #! alp-stdout: a\\tb\\n           expected alpshell stdout for a diff case (Python string escapes)
    #! alp-status: 2                 expected alpshell exit status for a diff case
    <script lines>

Each case runs in a fresh temporary directory with HOME=/usr, LC_ALL=C and a clean PATH.
"""
import argparse
import ast
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CASES = os.path.join(HERE, "script_cases.txt")


class Case:
    def __init__(self, name):
        self.name = name
        self.compare = {"stdout", "stderr", "status"}
        self.diff = None
        self.alp_stdout = None
        self.alp_status = None
        self.lines = []

    @property
    def script(self):
        return "\n".join(self.lines) + "\n"


def load_cases(path):
    cases, cur = [], None
    with open(path, encoding="utf-8") as f:
        for raw in f.read().split("\n"):
            m = re.match(r"^=== (\S+)\s*$", raw)
            if m:
                cur = Case(m.group(1))
                cases.append(cur)
            elif cur is None:
                continue  # text before the first case is documentation
            elif raw.startswith("#! "):
                key, _, value = raw[3:].partition(":")
                key, value = key.strip(), value.strip()
                if key == "compare":
                    cur.compare = set(value.split())
                elif key == "diff":
                    cur.diff = value
                elif key == "alp-stdout":
                    cur.alp_stdout = ast.literal_eval('"' + value + '"')
                elif key == "alp-status":
                    cur.alp_status = int(value)
                else:
                    raise SystemExit("unknown metadata %r in case %s" % (key, cur.name))
            else:
                cur.lines.append(raw)
    for c in cases:
        while c.lines and c.lines[-1] == "":
            c.lines.pop()
    return cases


def normalise_stderr(text, script_name):
    """Error messages differ only in their prefix: "case.sh: line 3: x" (bash) vs "alpshell: x"."""
    out = []
    for line in text.split("\n"):
        # bash names the script by the path it was given ("/tmp/x/case.sh: line 3: ..."), alpshell says "alpshell: ..."
        line = re.sub(r"^((\S*/)?%s|bash|alpshell): (line \d+: )?" % re.escape(script_name), "sh: ", line)
        out.append(line)
    return "\n".join(out)


def run(argv, script_path, workdir):
    env = {"PATH": "/usr/local/bin:/usr/bin:/bin", "HOME": "/usr", "LC_ALL": "C", "TERM": "dumb"}
    p = subprocess.run(argv + [script_path], cwd=workdir, env=env, capture_output=True, text=True, timeout=20)
    return p.stdout, p.stderr, p.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shell", default="build/alpshell")
    ap.add_argument("--bash", default="bash")
    ap.add_argument("--filter", default="")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    shell = os.path.abspath(args.shell)

    cases = [c for c in load_cases(CASES) if args.filter in c.name]
    failures, expected_diffs, passed = [], 0, 0
    for case in cases:
        with tempfile.TemporaryDirectory() as tmp:
            script_name = "case.sh"
            script_path = os.path.join(tmp, script_name)
            with open(script_path, "w", encoding="utf-8") as f:
                f.write(case.script)
            workdir = os.path.join(tmp, "work")
            os.mkdir(workdir)
            try:
                b_out, b_err, b_rc = run([args.bash], script_path, workdir)
            except subprocess.TimeoutExpired:
                failures.append((case.name, "bash timed out"))
                continue
            # a fresh work directory for alpshell so that files from the bash run cannot leak in
            workdir2 = os.path.join(tmp, "work2")
            os.mkdir(workdir2)
            try:
                a_out, a_err, a_rc = run([shell], script_path, workdir2)
            except subprocess.TimeoutExpired:
                failures.append((case.name, "alpshell timed out"))
                continue

        if case.diff is not None:
            problems = []
            if case.alp_stdout is not None and a_out != case.alp_stdout:
                problems.append("alpshell stdout %r, expected %r" % (a_out, case.alp_stdout))
            if case.alp_status is not None and a_rc != case.alp_status:
                problems.append("alpshell status %d, expected %d" % (a_rc, case.alp_status))
            if a_rc < 0 or a_rc >= 128 and case.alp_status is None:
                problems.append("alpshell died with status %d" % a_rc)
            if problems:
                failures.append((case.name, "; ".join(problems)))
            else:
                expected_diffs += 1
                if args.verbose:
                    print("DIFF  %-28s %s" % (case.name, case.diff))
            continue

        problems = []
        if "stdout" in case.compare and a_out != b_out:
            problems.append("stdout differs\n    bash:     %r\n    alpshell: %r" % (b_out, a_out))
        if "stderr" in case.compare:
            nb, na = normalise_stderr(b_err, script_name), normalise_stderr(a_err, script_name)
            if na != nb:
                problems.append("stderr differs\n    bash:     %r\n    alpshell: %r" % (nb, na))
        if "status" in case.compare and a_rc != b_rc:
            problems.append("exit status: bash %d, alpshell %d" % (b_rc, a_rc))
        if problems:
            failures.append((case.name, "\n  ".join(problems)))
        else:
            passed += 1
            if args.verbose:
                print("ok    %s" % case.name)

    print()
    for name, why in failures:
        print("FAIL  %s\n  %s" % (name, why))
    print("\n%d cases: %d match bash, %d expected differences, %d failed" % (len(cases), passed, expected_diffs, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
