#!/usr/bin/env python3
"""Validate and execute tests/integration/manifest.json (Python stdlib only)."""
from __future__ import annotations
import json, os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "manifest.json"

def load():
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or not isinstance(data.get("tests"), list):
        raise ValueError("unsupported or malformed manifest")
    return data["tests"]

def validate(cases):
    errors, names, sources = [], set(), set()
    for case in cases:
        name, source = case.get("name"), case.get("source")
        if not name or name in names:
            errors.append(f"missing or duplicate test name: {name!r}")
        names.add(name)
        if not source or Path(source).name != source or not source.endswith(".suru"):
            errors.append(f"{name}: source must be a top-level .suru file")
        else:
            sources.add(source)
            if not (HERE / source).is_file():
                errors.append(f"{name}: source file missing: {source}")
        if not isinstance(case.get("exit_code"), int):
            errors.append(f"{name}: exit_code must be an integer")
        stdout = case.get("stdout")
        if not isinstance(stdout, dict):
            errors.append(f"{name}: stdout must declare golden or unsupported")
        elif "golden" in stdout:
            golden = stdout["golden"]
            if Path(golden).name != golden or not (HERE / golden).is_file():
                errors.append(f"{name}: missing or invalid golden {golden}")
        elif not stdout.get("unsupported"):
            errors.append(f"{name}: unsupported stdout requires a reason")
        for fixture in case.get("fixtures", {}):
            if Path(fixture).is_absolute() or ".." in Path(fixture).parts:
                errors.append(f"{name}: unsafe fixture path {fixture}")
    actual = {p.name for p in HERE.glob("*.suru")}
    if actual != sources:
        errors.append(f"manifest/source drift: unlisted={sorted(actual-sources)}, missing={sorted(sources-actual)}")
    return errors

def run_case(executable, case):
    with tempfile.TemporaryDirectory(prefix="suru-corpus-") as td:
        fixture_dir = Path(td)
        for rel, contents in case.get("fixtures", {}).items():
            target = fixture_dir / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(contents, encoding="utf-8")
        env = os.environ.copy()
        for key, value in case.get("env", {}).items():
            env[key] = value.replace("{fixture_dir}", str(fixture_dir))
        command = [str(executable), "run", str(HERE / case["source"]), *case.get("args", [])]
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False)
        errors = []
        if result.returncode != case["exit_code"]:
            errors.append(f"exit {result.returncode}, expected {case['exit_code']}")
        stdout = case["stdout"]
        if "golden" in stdout:
            expected = (HERE / stdout["golden"]).read_bytes()
            if result.stdout != expected:
                errors.append(f"stdout differs from {stdout['golden']}: got={result.stdout!r} expected={expected!r}")
        if errors:
            print(f"FAIL: {case['name']}: {'; '.join(errors)}", file=sys.stderr)
            if result.stderr:
                sys.stderr.write(result.stderr.decode("utf-8", errors="replace"))
            return False
        print(f"ok: {case['name']} [{'exact' if 'golden' in stdout else 'stdout-unsupported'}]")
        return True

def main():
    cases = load()
    if len(sys.argv) > 1 and sys.argv[1] == "--count-only":
        print(len(cases)); return 0
    errors = validate(cases)
    if errors:
        for err in errors: print(f"manifest error: {err}", file=sys.stderr)
        return 2
    if len(sys.argv) > 1 and sys.argv[1] == "--validate-only":
        print(f"manifest: {len(cases)} cases validated"); return 0
    if len(sys.argv) < 2:
        print(f"usage: {Path(sys.argv[0]).name} SURU_EXECUTABLE", file=sys.stderr); return 2
    executable = Path(sys.argv[1]).resolve()
    if not executable.is_file():
        print(f"compiler executable not found: {executable}", file=sys.stderr); return 2
    failures = sum(not run_case(executable, case) for case in cases)
    unsupported = sum("unsupported" in case["stdout"] for case in cases)
    print(f"corpus: {len(cases)} executions, {failures} failures, {unsupported} stdout-unsupported source cases")
    return 1 if failures else 0

if __name__ == "__main__":
    raise SystemExit(main())
