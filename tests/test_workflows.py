"""Static checks for the GitHub Actions workflows (permissions, pins, scripts)."""

from __future__ import annotations

import re
from pathlib import Path

import yaml

WORKFLOWS = Path(__file__).resolve().parent.parent / ".github" / "workflows"
PINNED = re.compile(r"^[^@]+@[0-9a-f]{40}$")


def _workflows() -> dict[str, dict]:
    return {
        p.name: yaml.safe_load(p.read_text()) for p in sorted(WORKFLOWS.glob("*.yml"))
    }


def _jobs():
    for name, wf in _workflows().items():
        for job_id, job in wf["jobs"].items():
            yield name, job_id, job


def test_there_are_workflows():
    assert _workflows()


def test_every_job_sets_permissions():
    assert [f"{n}:{j}" for n, j, job in _jobs() if "permissions" not in job] == []


def test_actions_are_pinned_to_commits():
    loose = [
        f"{n}:{j}: {step['uses']}"
        for n, j, job in _jobs()
        for step in job.get("steps", [])
        if "uses" in step and not PINNED.match(step["uses"])
    ]
    assert loose == []


def test_no_expressions_inside_run_scripts():
    found = [
        f"{n}:{j}"
        for n, j, job in _jobs()
        for step in job.get("steps", [])
        if "${{" in step.get("run", "")
    ]
    assert found == []


def test_checkouts_drop_credentials_unless_the_job_pushes():
    keep = []
    for n, j, job in _jobs():
        pushes = any("git push" in step.get("run", "") for step in job.get("steps", []))
        for step in job.get("steps", []):
            if step.get("uses", "").startswith("actions/checkout@") and not pushes:
                if (step.get("with") or {}).get("persist-credentials") is not False:
                    keep.append(f"{n}:{j}")
    assert keep == []


def test_gate_job_needs_every_check_job():
    jobs = _workflows()["build.yml"]["jobs"]
    gate = jobs["ci"]
    publishing = {"release", "sync-dev", "test-build", "ci"}
    checks = {name for name in jobs if name not in publishing}
    assert set(gate["needs"]) == checks
    assert gate["if"] == "always()"
    assert gate["permissions"] == {}
