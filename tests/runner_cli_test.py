#!/usr/bin/env python3
"""Exercise the real core-test executable, including failure exit statuses."""
from pathlib import Path
import re
import subprocess
import sys

binary = str(Path(sys.argv[1]).resolve())


def run(*arguments, status=0):
    process = subprocess.run([binary, *arguments], capture_output=True, text=True, timeout=30)
    assert process.returncode == status, (arguments, process.returncode, process.stdout, process.stderr)
    return re.sub(r"\x1b\[[0-9;]*m", "", process.stdout), process.stderr


all_tests, errors = run("--list")
assert not errors, errors
names = all_tests.splitlines()
assert names and len(names) == len(set(names))
assert all(re.fullmatch(r"[a-z0-9_]+", name) for name in names), names
# The declarative rule case used to rely on an earlier test initializing the
# shared temporary arena. It must also work as the only selected test.
for pattern in ("window_close_", "window_constraints_", "test_runner_",
                "declarative_rule_apply_warns_only_after_first_matching_root",
                "declarative_reload_grammar_rejects_scoped_trailing_and_mixed_before_mutation",
                "window_management_rejects_zero_area_helpers_but_preserves_explicit_rules"):
    expected = [name for name in names if pattern in name]
    assert expected
    for arguments in (("--list", "--filter", pattern), ("--filter", pattern, "--list")):
        output, errors = run(*arguments)
        assert output.splitlines() == expected and not errors, (output, errors)
    output, _ = run("--filter", pattern)
    assert f"-- Running {len(expected)} tests" in output, output
    assert f"{len(expected)} succeeded" in output and "0 failed" in output, output
    for name in names:
        assert (f"success {name}\n" in output) == (name in expected), name

for arguments in (("--list", "--filter", "NO_SUCH_TEST"), ("--filter", "NO_SUCH_TEST")):
    output, errors = run(*arguments, status=1)
    assert not output and "no tests match" in errors
for arguments in (("--filter",), ("--filter", ""), ("--filter", "--list"),
                  ("--unknown",), ("unexpected",), ("--list", "--list"),
                  ("--filter", "one", "--filter", "two"), ("--help", "--list"),
                  ("--benchmark-unknown",)):
    output, errors = run(*arguments, status=2)
    assert not output and "usage:" in errors, (output, errors)
output, errors = run("--help")
assert "usage:" in output and "Running" not in output and not errors
output, errors = run("--benchmark-window-constraints")
assert '"case":"window-constraints"' in output and "Running" not in output and not errors
print(f"runner CLI regression checks passed ({len(names)} discoverable tests)")
