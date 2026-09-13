#!/usr/bin/env python3
"""Guard that the accessibility-smoke QML mirrors stay byte-identical.

`ProductQuickAccessibilitySmoke/CMakeLists.txt` states the contract:

    LoopEditor/qml/ remains the single source of truth; keep every mirror
    byte-identical when either side changes.

Nothing enforced it, and this session proved what that costs. `4acb0560`
("feat(editor): complete preflight workflow") grew `LoopEditor/qml/
PreflightPane.qml` by 134 lines - a variables Repeater, Run/Cancel/Export
controls and a ProgressBar - and touched no mirror, so the accessibility
evidence kept describing a pane the product no longer shipped; the drift was
found only by reading both files. The same commit left `LoopEditor/qml/Main.qml`
ahead of its twin by the preflight export FileDialog.

The two directories also must not drift apart in *shape*: a QML file present on
one side only is either an unmirrored product surface or a mirror for a file
that no longer exists, and both mean the smoke no longer exercises what ships.

Byte-identical is literal: the comparison is over file bytes, so a line-ending
or trailing-newline difference is a violation too.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = "LoopEditor/qml"
MIRROR_ROOT = "ProductQuickAccessibilitySmoke/qml"


@dataclass(frozen=True)
class Violation:
    path: str
    line: int
    rule: str
    detail: str

    def format(self) -> str:
        return f"{self.path}:{self.line}: {self.rule} {self.detail}"


def read_tree(root_name: str) -> dict[str, bytes]:
    """Every `*.qml` under `root_name`, keyed by its path relative to that root."""
    root = ROOT / root_name
    if not root.is_dir():
        return {}
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*.qml"))
        if path.is_file()
    }


def source_texts() -> dict[str, bytes]:
    return read_tree(SOURCE_ROOT)


def mirror_texts() -> dict[str, bytes]:
    return read_tree(MIRROR_ROOT)


def first_difference(left: list[bytes], right: list[bytes]) -> int:
    for index, (left_line, right_line) in enumerate(zip(left, right)):
        if left_line != right_line:
            return index + 1
    return min(len(left), len(right)) + 1


def compare_mirrors(
    sources: dict[str, bytes],
    mirrors: dict[str, bytes],
    source_root: str = SOURCE_ROOT,
    mirror_root: str = MIRROR_ROOT,
) -> list[Violation]:
    """Report every pair that is not byte-identical, and every one-sided file."""
    violations: list[Violation] = []
    for name in sorted(sources):
        source_path = f"{source_root}/{name}"
        mirror_path = f"{mirror_root}/{name}"
        if name not in mirrors:
            violations.append(
                Violation(
                    source_path,
                    1,
                    "mirror-missing",
                    f"(no {mirror_path} twin)",
                )
            )
            continue
        if sources[name] == mirrors[name]:
            continue
        source_lines = sources[name].splitlines()
        mirror_lines = mirrors[name].splitlines()
        violations.append(
            Violation(
                source_path,
                first_difference(source_lines, mirror_lines),
                "mirror-drift",
                f"vs {mirror_path} (source {len(source_lines)} lines, mirror {len(mirror_lines)} lines)",
            )
        )
    for name in sorted(mirrors):
        if name not in sources:
            violations.append(
                Violation(
                    f"{mirror_root}/{name}",
                    1,
                    "mirror-orphan",
                    f"(no {source_root}/{name} source)",
                )
            )
    return violations


def main() -> int:
    sources = source_texts()
    mirrors = mirror_texts()
    if not sources or not mirrors:
        print(
            f"ERROR: QML mirror-parity guard found no files under {SOURCE_ROOT} / {MIRROR_ROOT}",
            file=sys.stderr,
        )
        return 1

    violations = compare_mirrors(sources, mirrors)
    if violations:
        print("ERROR: QML mirror-parity guard failed:", file=sys.stderr)
        for violation in violations:
            print(violation.format(), file=sys.stderr)
        return 1

    print(f"QML mirror-parity guard passed: {len(sources)} mirror pair(s) are byte-identical.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
