#!/usr/bin/env python3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT.parent / "godot-4.7" / "modules" / "gdscript" / "tests" / "scripts"
MARKER = "ERROR: CHECK( result.passed ) is NOT correct!"


def failures(log_text):
    for block in log_text.split(MARKER)[1:]:
        lines = block.splitlines()
        logged = next((index for index, line in enumerate(lines) if "logged:" in line), None)
        if logged is None:
            continue
        script = lines[logged].split("logged:", 1)[1].strip()
        actual = []
        for line in lines[logged + 1 :]:
            if line.startswith("modules/gdscript/tests/gdscript_test_runner.cpp") or line.startswith("==="):
                break
            actual.append(line.strip())
        while actual and not actual[-1]:
            actual.pop()
        yield script, actual


def main():
    log = (ROOT / "build" / "conformance" / "run.log").read_text(encoding="utf-8", errors="replace")
    pattern = sys.argv[1] if len(sys.argv) > 1 else ""
    for script, actual in failures(log):
        if pattern not in script:
            continue
        expected_path = Path(script).with_suffix(".out")
        expected = [line.strip() for line in expected_path.read_text(encoding="utf-8").strip().splitlines()] if expected_path.exists() else []
        relative = script.split("tests/scripts/", 1)[-1]
        print(f"== {relative}")
        for index in range(1, max(len(actual), len(expected))):
            got = actual[index] if index < len(actual) else "<missing>"
            want = expected[index] if index < len(expected) else "<missing>"
            if got != want:
                print(f"   expected: {want}")
                print(f"   actual:   {got}")
                break


if __name__ == "__main__":
    main()
