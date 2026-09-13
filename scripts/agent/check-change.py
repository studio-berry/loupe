#!/usr/bin/env python3
"""Run the policy-selected proof checks for a change."""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[2]
POLICY_PATH = ROOT / "agent-policy.json"
FIELD_RE = re.compile(r"^(Category|Audience|Breaking-Change|Summary):\s*(.*?)\s*$")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
NON_TEST_CHECKS = {"architecture_catalog", "policy_adapters"}


@dataclass
class Change:
    status: str
    path: str
    old_path: str | None = None


@dataclass
class Evidence:
    name: str
    command: list[str] = field(default_factory=list)
    result: str = "not-run"
    reason: str | None = None
    duration_ms: int = 0
    output: str = ""

    def as_dict(self) -> dict:
        value = {
            "name": self.name,
            "result": self.result,
            "duration_ms": self.duration_ms,
        }
        if self.command:
            value["command"] = self.command
        if self.reason:
            value["reason"] = self.reason
        if self.output:
            value["output"] = self.output[-4000:]
        return value


def load_policy() -> dict:
    with POLICY_PATH.open(encoding="utf-8") as stream:
        return json.load(stream)


def run_git(args: list[str]) -> str:
    completed = subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True)
    return completed.stdout


def resolve_revision(revision: str) -> str:
    return run_git(["rev-parse", "--verify", f"{revision}^{{commit}}"]).strip()


def resolve_merge_base(base: str, head: str) -> str:
    return run_git(["merge-base", base, head]).strip()


def parse_name_status(raw: bytes) -> list[Change]:
    fields = raw.decode("utf-8", errors="surrogateescape").split("\0")
    changes: list[Change] = []
    index = 0
    while index < len(fields) and fields[index]:
        status = fields[index]
        index += 1
        if status.startswith("R") or status.startswith("C"):
            old_path = fields[index]
            path = fields[index + 1]
            index += 2
            changes.append(Change(status[0], path, old_path))
        else:
            changes.append(Change(status[0], fields[index]))
            index += 1
    return changes


def diff_changes(base: str, head: str) -> list[Change]:
    completed = subprocess.run(
        ["git", "-C", str(ROOT), "diff", "--name-status", "-z", f"{base}..{head}"],
        check=True,
        capture_output=True,
    )
    return parse_name_status(completed.stdout)


def branch_slug(branch: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9._-]+", "-", branch.strip())
    slug = slug.strip("-.")
    return slug or "change"


def current_branch(override: str | None) -> str:
    if override:
        return override
    for key in ("GITHUB_HEAD_REF", "CI_HEAD_BRANCH"):
        if os.environ.get(key):
            return os.environ[key]
    try:
        return run_git(["symbolic-ref", "--short", "HEAD"]).strip()
    except subprocess.CalledProcessError:
        return "detached"


def policy_integration_branches(policy: dict) -> set[str]:
    branches = policy.get("branches", {})
    names: set[str] = set(branches.get("protected") or [])
    for key in ("integration", "qualification", "release", "default"):
        value = branches.get(key)
        if isinstance(value, str) and value:
            names.add(value)
    return names


def skip_changelog_reason(branch: str, policy: dict, skip: bool) -> str | None:
    """Changelog fragments are named after topic-branch PRs, not integration pushes."""
    if skip:
        return "non-PR event"
    event = os.environ.get("GITHUB_EVENT_NAME")
    if event and event != "pull_request":
        return "non-PR event"
    if branch in policy_integration_branches(policy):
        return "integration branch"
    return None


def changelog_evidence(changes: list[Change], policy: dict, branch: str, skip: bool) -> Evidence:
    reason = skip_changelog_reason(branch, policy, skip)
    if reason:
        return Evidence("changelog", result="not-applicable", reason=reason)
    return check_changelog(changes, policy, branch)


def format_sources(changes: Iterable[Change]) -> list[str]:
    return sorted(
        {
            change.path
            for change in changes
            if change.status != "D" and Path(change.path).suffix.lower() in SOURCE_SUFFIXES
        }
    )


def classify(changes: Iterable[Change], policy: dict) -> list[str]:
    modules: set[str] = set()
    definitions = policy["module_boundaries"]
    for change in changes:
        paths = [change.path]
        if change.old_path:
            paths.append(change.old_path)
        for path in paths:
            for module, definition in definitions.items():
                if any(fnmatch.fnmatchcase(path, pattern) for pattern in definition["paths"]):
                    modules.add(module)
    return sorted(modules) or ["unclassified"]


def selected_values(modules: Iterable[str], policy: dict, key: str) -> list[str]:
    values: set[str] = set()
    for module in modules:
        definition = policy["module_boundaries"].get(module)
        if definition:
            values.update(definition[key])
    return sorted(values)


def protected_paths(changes: Iterable[Change], policy: dict) -> list[str]:
    protected = policy.get("protected_paths", [])
    return sorted(
        {
            path
            for change in changes
            for path in (change.path, change.old_path)
            if path and any(fnmatch.fnmatchcase(path, pattern) for pattern in protected)
        }
    )


def parse_changelog(path: Path, allowed_categories: set[str]) -> tuple[bool, str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        return False, str(exc)
    fields: dict[str, str] = {}
    for line in lines:
        match = FIELD_RE.match(line)
        if match:
            fields[match.group(1)] = match.group(2)
    required = {"Category", "Audience", "Breaking-Change", "Summary"}
    missing = sorted(required - fields.keys())
    if missing:
        return False, f"missing fields: {', '.join(missing)}"
    if fields["Category"] not in allowed_categories:
        return False, f"invalid Category: {fields['Category']}"
    if fields["Breaking-Change"].lower() not in {"yes", "no"}:
        return False, "Breaking-Change must be yes or no"
    if not fields["Audience"] or not fields["Summary"]:
        return False, "Audience and Summary must not be empty"
    return True, ""


def check_changelog(changes: list[Change], policy: dict, branch: str) -> Evidence:
    directory = policy["changelog"]["directory"].rstrip("/")
    expected = f"{directory}/{branch_slug(branch)}.md"
    fragments = [
        change.path
        for change in changes
        if change.status in {"A", "M"}
        and change.path.startswith(f"{directory}/")
        and change.path.endswith(".md")
    ]
    evidence = Evidence("changelog")
    if expected not in fragments:
        evidence.result = "fail"
        evidence.reason = f"expected changelog fragment {expected}; found {fragments or 'none'}"
        return evidence
    categories = set(policy["changelog"]["categories"])
    valid, reason = parse_changelog(ROOT / expected, categories)
    if not valid:
        evidence.result = "fail"
        evidence.reason = f"{expected}: {reason}"
        return evidence
    evidence.result = "pass"
    return evidence


def add_result(evidence: list[Evidence], name: str, command: list[str], cwd: Path, dry_run: bool) -> None:
    item = Evidence(name, command)
    if dry_run:
        item.result = "not-run"
        item.reason = "dry-run"
        evidence.append(item)
        return
    started = time.monotonic()
    try:
        completed = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    except (FileNotFoundError, OSError) as exc:
        item.result = "incomplete"
        item.reason = f"prerequisite unavailable: {exc}"
    else:
        item.result = "pass" if completed.returncode == 0 else "fail"
        item.output = (completed.stdout + completed.stderr).strip()
        if completed.returncode != 0:
            item.reason = f"exit code {completed.returncode}"
    item.duration_ms = round((time.monotonic() - started) * 1000)
    evidence.append(item)


def add_format_checks(
    evidence: list[Evidence], sources: list[str], *, dry_run: bool, fix_format: bool
) -> None:
    if dry_run:
        for source in sources:
            add_result(
                evidence,
                f"format:{source}",
                ["clang-format", "--dry-run", "--Werror", source],
                ROOT,
                True,
            )
        return

    if not shutil.which("clang-format"):
        evidence.append(
            Evidence(
                "format",
                ["clang-format", "--dry-run"],
                result="incomplete",
                reason="prerequisite unavailable: clang-format",
            )
        )
        return

    if fix_format:
        for source in sources:
            subprocess.run(["clang-format", "-i", source], cwd=ROOT, check=True)
    for source in sources:
        add_result(
            evidence,
            f"format:{source}",
            ["clang-format", "--dry-run", "--Werror", source],
            ROOT,
            False,
        )


def uses_manual_moc_include(source: str, root: Path = ROOT) -> bool:
    try:
        text = (root / source).read_text(encoding="utf-8")
    except OSError:
        return False
    return '#include "' in text and '.moc"' in text


def clang_tidy_sources(sources: list[str]) -> list[str]:
    implementation_suffixes = {".c", ".cc", ".cpp", ".cxx"}
    return [
        source
        for source in sources
        if Path(source).suffix.lower() in implementation_suffixes
        and not uses_manual_moc_include(source)
    ]


CLANG_TIDY_CANDIDATES = ("clang-tidy-18", "clang-tidy-19", "clang-tidy")


def find_clang_tidy() -> str | None:
    """Return the first available clang-tidy binary, or None.

    CI installs the version the toolchain pins (`clang-tidy-18`), but a developer
    machine may only carry an unversioned binary (both the LLVM installer and the
    PyPI wheels ship `clang-tidy`), so try the pinned names first and fall back to
    the plain one instead of reporting the whole lane unavailable.
    """
    for candidate in CLANG_TIDY_CANDIDATES:
        executable = shutil.which(candidate)
        if executable:
            return executable
    return None


def add_clang_tidy_checks(
    evidence: list[Evidence], sources: list[str], build_dir: Path, *, dry_run: bool
) -> None:
    sources = clang_tidy_sources(sources)
    if dry_run:
        for source in sources:
            add_result(
                evidence,
                f"clang_tidy:{source}",
                [CLANG_TIDY_CANDIDATES[0], "-p", str(build_dir), "--quiet", source],
                ROOT,
                True,
            )
        return

    compile_db = build_dir / "compile_commands.json"
    clang_tidy = find_clang_tidy()
    if compile_db.exists() and clang_tidy:
        for source in sources:
            add_result(
                evidence,
                f"clang_tidy:{source}",
                [clang_tidy, "-p", str(build_dir), "--quiet", source],
                ROOT,
                False,
            )
        return

    evidence.append(
        Evidence(
            "clang_tidy",
            [CLANG_TIDY_CANDIDATES[0], "-p", str(build_dir)],
            result="incomplete",
            reason=f"compile_commands.json or clang-tidy unavailable (tried: {', '.join(CLANG_TIDY_CANDIDATES)})",
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, help="base commit or ref")
    parser.add_argument("--head", default="HEAD", help="head commit or ref")
    parser.add_argument("--head-branch", help="branch name used for changelog validation")
    parser.add_argument("--build-dir", default="build", help="existing configured build directory")
    parser.add_argument("--report", type=Path, help="write JSON evidence to this path")
    parser.add_argument("--dry-run", action="store_true", help="plan checks without executing them")
    parser.add_argument("--fix-format", action="store_true", help="format changed C/C++ files before checking")
    parser.add_argument("--skip-changelog", action="store_true", help="skip PR-only changelog validation for non-PR release events")
    args = parser.parse_args()

    evidence: list[Evidence] = []
    try:
        policy = load_policy()
        base_sha = resolve_revision(args.base)
        head_sha = resolve_revision(args.head)
        comparison_base_sha = resolve_merge_base(base_sha, head_sha)
        changes = diff_changes(comparison_base_sha, head_sha)
    except (OSError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: unable to establish change set: {exc}", file=sys.stderr)
        return 1

    branch = current_branch(args.head_branch)
    modules = classify(changes, policy)
    sources = format_sources(changes)
    targets = selected_values(modules, policy, "targets")
    tests = [test for test in selected_values(modules, policy, "tests") if test not in NON_TEST_CHECKS]
    protected = protected_paths(changes, policy)
    build_dir = (ROOT / args.build_dir).resolve()

    evidence.append(changelog_evidence(changes, policy, branch, args.skip_changelog))
    python = sys.executable
    add_result(evidence, "source_integrity", [python, "scripts/ci/check_source_integrity.py"], ROOT, args.dry_run)
    add_result(evidence, "architecture_catalog", [python, "scripts/generate-architecture-catalogs.py", "--check"], ROOT, args.dry_run)
    add_result(evidence, "policy_adapters", [python, "scripts/agent/generate-adapters.py"], ROOT, args.dry_run)
    # Whole-tree contract guards: they audit invariants of the tree, not just the
    # diff, so they run on every change like source_integrity does.
    add_result(
        evidence,
        "preflight_truth_source",
        [python, "scripts/ci/check_preflight_truth_source.py"],
        ROOT,
        args.dry_run,
    )
    add_result(
        evidence,
        "qml_mirror_parity",
        [python, "scripts/ci/check_qml_mirror_parity.py"],
        ROOT,
        args.dry_run,
    )

    if sources:
        add_format_checks(
            evidence,
            sources,
            dry_run=args.dry_run,
            fix_format=args.fix_format,
        )

    for target in targets:
        add_result(evidence, f"build:{target}", ["cmake", "--build", str(build_dir), "--target", target, "--config", "Release"], ROOT, args.dry_run)
    for test in tests:
        add_result(evidence, f"build:{test}", ["cmake", "--build", str(build_dir), "--target", test, "--config", "Release"], ROOT, args.dry_run)

    if sources:
        add_clang_tidy_checks(evidence, sources, build_dir, dry_run=args.dry_run)
    if tests:
        expression = "^(" + "|".join(re.escape(test) for test in tests) + ")$"
        add_result(evidence, "focused_tests", ["ctest", "--test-dir", str(build_dir), "--output-on-failure", "-R", expression], ROOT, args.dry_run)

    report = {
        "format_version": 1,
        "base_sha": base_sha,
        "comparison_base_sha": comparison_base_sha,
        "head_sha": head_sha,
        "head_branch": branch,
        "changed_paths": [{"status": change.status, "path": change.path, **({"old_path": change.old_path} if change.old_path else {})} for change in changes],
        "modules": modules,
        "risk": "high" if protected else "standard",
        "protected_paths": protected,
        "targets": targets,
        "tests": tests,
        "checks": [item.as_dict() for item in evidence],
    }
    if any(item.result == "fail" for item in evidence):
        report["status"] = "fail"
    elif any(item.result == "incomplete" for item in evidence):
        report["status"] = "incomplete"
    else:
        report["status"] = "pass"
    print(json.dumps(report, indent=2, sort_keys=True))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
