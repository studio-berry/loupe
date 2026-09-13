#!/usr/bin/env python3
"""Guard that the GUI computes no preflight pass or severity logic.

Issue #195, "No pass/severity logic is computed in GUI code". Core owns the
truth: `pdf::reducePreflightVerdict()` yields the verdict state,
`pdfinteraction::PreflightController` owns the state machine, and
`pdfquick::tokens::resolvePreflightStateVisual()` maps Core's own state name onto
the canonical #194 treatment. The GUI renders that answer; it never derives one.

This check therefore fails when the QML or the Editor presentation layer:

  * reads Core's completeness flag (`inspectionComplete`),
  * aggregates findings to choose copy, colour or state -- `errors.length`,
    `errors.isEmpty()`, `warnings.length`, a findings model's `rowCount`
    (`findingsModel.rowCount === 0`), `findings.length`, a namespaced count
    (`result.stats.errorCount`, `warningCount`),
  * writes a pass/fail verdict literal instead of rendering the canonical state
    visual: a capitalized or uppercase verdict word (`PASS`/`Passed`, `FAIL`/
    `Failed`) rendered as user-visible copy, whichever the quoting -- double
    quotes, single quotes or a backtick template literal are all copy. Lower-case
    `pass`/`fail` are not flagged: they are Core's own state-name data (Core's
    `PreflightController::State::Pass` maps to `"pass"` in editorhost.cpp), and
    the smoke harness prints them as its own status, not as preflight verdicts.

The design is deny-only. `RULES` is the whole policy: there is no allow-list and
no suppression, so no surface can launder a forbidden pattern past a rule. What
the GUI *may* do is render the surfaces named in `RENDER_ONLY_SURFACES` below --
Core's own preflight properties and invokables on EditorHost plus the
controller-owned model and selection. Those names appear in no rule, and
`test_every_core_owned_surface_renders_without_a_violation` pins a representative
use of each as clean.

Comment and string awareness. Each rule scans a view of the line selected by its
`view` field:

  * `code` (the completeness and aggregation rules): comments and the *contents*
    of `"..."`, `'...'` and backtick literals are blanked, delimiters and
    newlines kept. An identifier read inside `//`, `/* ... */` or human-facing
    wording (`qsTr("inspectionComplete")`) is therefore not mistaken for a read.
  * `uncommented` (the verdict-copy rule): comments are blanked but string
    contents are kept, because a string literal is exactly where user-visible
    verdict copy lives and rendering it is the violation regardless of quoting.

Scope: `ProductQuickAccessibilitySmoke/qml/**`, `ProductQuickAccessibilitySmoke/
main.cpp`, `LoopEditor/qml/**` and `LoopEditor/editorhost.cpp`. The whole of
editorhost.cpp is in scope on purpose: it is the QML-facing host, and it holds no
Core reducer call (`grep -n reducePreflightVerdict LoopEditor/editorhost.cpp` is
empty), so the presentation half and the file are the same thing today.

Files are *discovered*: every `*.qml` under the two QML roots, plus the two host
files. A new pane therefore joins the scan automatically, and
`test_scope_file_list_is_exactly_the_expected_set` pins the discovered names for
this tree, so adding a pane fails that test until the name is added -- the point
being that the new pane is reviewed against the rules rather than sliding into
scope unnoticed (`22 GUI file(s)` in this check's own output becomes 23).
"""

from __future__ import annotations

import fnmatch
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

# The GUI layers. `*` also covers a nested directory under either qml root.
SCOPE_PATTERNS = (
    "ProductQuickAccessibilitySmoke/qml/*.qml",
    "ProductQuickAccessibilitySmoke/main.cpp",
    "LoopEditor/qml/*.qml",
    "LoopEditor/editorhost.cpp",
)
SCOPE_DIRECTORIES = (
    "ProductQuickAccessibilitySmoke/qml",
    "LoopEditor/qml",
)
SCOPE_FILES = (
    "ProductQuickAccessibilitySmoke/main.cpp",
    "LoopEditor/editorhost.cpp",
)

# The views a rule can be matched against; see the module docstring.
CODE = "code"
UNCOMMENTED = "uncommented"


@dataclass(frozen=True)
class Rule:
    id: str
    pattern: re.Pattern[str]
    detail: str
    view: str = CODE


@dataclass(frozen=True)
class Violation:
    path: str
    line: int
    rule: str
    detail: str

    def format(self) -> str:
        return f"{self.path}:{self.line}: {self.rule}: {self.detail}"


# The one aggregate property name that matters: a count, a size or an emptiness
# test on a finding collection is severity logic, whichever collection it is.
AGGREGATE_MEMBERS = r"(?:length|count|size|rowCount|isEmpty)"

RULES = (
    Rule(
        "gui-derives-completeness",
        re.compile(r"\binspectionComplete\b"),
        "reads Core's completeness flag; only pdf::reducePreflightVerdict() may judge an inspection complete",
    ),
    Rule(
        "gui-aggregates-findings",
        re.compile(
            # errors.length / warnings.isEmpty / errors.count ...
            rf"\b(?:errors|warnings)\s*\.\s*{AGGREGATE_MEMBERS}\b"
            # findings.length / findingsModel.rowCount / findingsModel()->rowCount()
            rf"|\bfindings\w*\s*(?:\(\s*\))?\s*(?:\.|->)\s*{AGGREGATE_MEMBERS}\b"
            # result.stats.errorCount / warningCount / issueCount / findingCount
            r"|\b(?:error|warning|issue|finding)Count\b"
        ),
        "aggregates finding counts into a verdict; severity is PreflightController's to report",
    ),
    Rule(
        "gui-derives-pass-fail-copy",
        re.compile(r"""["'`]\s*(?:PASS|PASSED|FAIL|FAILED|Pass|Passed|Fail|Failed)\s*["'`]"""),
        "writes a pass/fail verdict literal; render preflightStateVisual / preflightOperatorSummary instead",
        view=UNCOMMENTED,
    ),
)

# RENDER_ONLY_SURFACES -- the surfaces the GUI may render, and why rendering them
# derives nothing. Deliberately prose, not data: the guard is deny-only, so
# `scan_text`/`scan_tree` consult nothing here and nothing here can suppress a
# rule. A name listed below still fires if it is combined with a forbidden
# pattern, which is the property the old ALLOWED_SURFACES dict claimed and did
# not have (`test_guard_is_deny_only_and_exposes_no_allow_list` now asserts the
# absence of any such structure).
#
# Core-owned preflight properties and invokables on EditorHost/LoopEditor/editorhost.cpp
# (each one is Core's own answer, already resolved by LoopLibCore / LoopLibQuick /
# LoopLibInteraction; reading them is rendering, interpreting them is not):
#   preflightOperatorSummary     Core's own operator wording (PreflightController::operatorSummary)
#   preflightStateName           Core's own state name (PreflightController::State via preflightStateToString)
#   preflightStateVisual         the canonical #194 treatment resolved by pdfquick::tokens::resolvePreflightStateVisual
#   preflightStateColor          the same visual's colour, looked up by role in LoopLibQuick's tokens
#   preflightProfiles            the profile enumeration Core resolved, with its own validity diagnostics
#   preflightVariables           the selected profile's variable bindings as Core resolved them
#   selectedPreflightProfileId   the controller's current profile selection
#   hasPreflightReport           the controller's report-presence flag
#   runPreflight, cancelPreflight, selectPreflightProfile, setPreflightVariable,
#   requestPreflightReportExport, exportPreflightReportFileUrl
#                                preflight Q_INVOKABLE commands; they ask Core, they do not decide
#
# Controller-owned and bound read-only by the shipped pane
# (ProductQuickAccessibilitySmoke/qml/PreflightPane.qml and its LoopEditor/qml mirror):
#   findingsModel                the findings list model (:11, used at :148) -- render its
#                                rows and navigate them; do not count it to pick a verdict
#   progress                     the job's own progress value (:135), already Core's
#   selection                    the controller's finding selection (selectFinding / findingIdAt)
#
# Lookups that stay clean and must keep doing so: findingsModel.findingIdAt(...)
# and findingsView.width are not aggregates, and pageCount / searchResultCount /
# childCount are not finding counts -- pattern the aggregate rule to the finding
# vocabulary, not to every `.count` in the tree.


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def in_scope(path: str) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in SCOPE_PATTERNS)


def gui_sources() -> list[str]:
    paths: list[str] = []
    for directory in SCOPE_DIRECTORIES:
        root = ROOT / directory
        if root.is_dir():
            paths.extend(sorted(relative(path) for path in root.rglob("*.qml") if path.is_file()))
    for name in SCOPE_FILES:
        if (ROOT / name).is_file():
            paths.append(name)
    return sorted(path for path in paths if in_scope(path))


def _blank(character: str) -> str:
    """Blank a character for the code view, keeping every newline intact."""
    return "\n" if character == "\n" else " "


_CLOSING = {"double": '"', "single": "'", "template": "`"}


def views(text: str) -> tuple[str, str]:
    """Return the `(code, uncommented)` views of `text`, same length.

    `code` blanks comment text and the contents of `"..."`, `'...'` and backtick
    literals, keeping the delimiters and every newline, so an identifier that
    only appears in a comment or in human-facing wording is not mistaken for a
    read. `uncommented` blanks comments only and keeps string contents, so
    user-visible verdict copy is still seen. Because both views preserve the
    original newlines exactly, a line number from either is the real one.

    Backtick template literals are the exception to "whole literal is content":
    a `${ ... }` interpolation is code again, so a derivation inside it is still
    seen. The interpolation is copied through whole, with brace-depth matching
    only -- a `}` inside a nested string literal inside the interpolation would
    end it early, which is a known, deliberate limit of not parsing JavaScript
    here (no template literal is shipped in the GUI today).
    """
    code: list[str] = []
    uncommented: list[str] = []
    state = "normal"
    index = 0
    length = len(text)

    while index < length:
        character = text[index]
        pair = text[index : index + 2]

        if state == "normal":
            if pair == "//":
                code.append("  ")
                uncommented.append("  ")
                index += 2
                state = "line"
                continue
            if pair == "/*":
                code.append("  ")
                uncommented.append("  ")
                index += 2
                state = "block"
                continue
            if character in '"\'`':
                code.append(character)
                uncommented.append(character)
                index += 1
                state = {"\"": "double", "'": "single", "`": "template"}[character]
                continue
            code.append(character)
            uncommented.append(character)
            index += 1
            continue

        if state == "line":
            code.append(_blank(character))
            uncommented.append(_blank(character))
            if character == "\n":
                state = "normal"
            index += 1
            continue

        if state == "block":
            if pair == "*/":
                code.append("  ")
                uncommented.append("  ")
                index += 2
                state = "normal"
                continue
            code.append(_blank(character))
            uncommented.append(_blank(character))
            index += 1
            continue

        # Inside a string literal: comments do not start here, so `//` and `/*`
        # stay content; the code view blanks the contents, the other keeps them.
        if character == "\\":
            following = text[index + 1] if index + 1 < length else ""
            code.append(_blank(character))
            uncommented.append(character)
            if following:
                code.append(_blank(following))
                uncommented.append(following)
                index += 2
            else:
                index += 1
            continue

        if state == "template" and pair == "${":
            depth = 1
            end = index + 2
            while end < length and depth:
                if text[end] == "{":
                    depth += 1
                elif text[end] == "}":
                    depth -= 1
                end += 1
            interpolation = text[index:end]
            code.append(interpolation)
            uncommented.append(interpolation)
            index = end
            continue

        if character == _CLOSING[state]:
            code.append(character)
            uncommented.append(character)
            index += 1
            state = "normal"
            continue

        code.append(_blank(character))
        uncommented.append(character)
        index += 1

    return "".join(code), "".join(uncommented)


def scan_text(path: str, text: str) -> list[Violation]:
    """Report every forbidden pattern in `text`, as `path:line: rule`.

    Each rule is matched against the view its `Rule.view` selects: "code"
    (comments and string contents blanked) for reads and aggregations,
    "uncommented" (comments blanked, string contents kept) for verdict copy.
    """
    code, uncommented = views(text)
    violations: list[Violation] = []
    # Both views keep the newlines of the original text, so `split("\n")` keeps
    # the two line lists in lockstep with the real line numbers.
    for number, (code_line, uncommented_line) in enumerate(zip(code.split("\n"), uncommented.split("\n")), start=1):
        for rule in RULES:
            line = code_line if rule.view == CODE else uncommented_line
            if rule.pattern.search(line):
                violations.append(Violation(path, number, rule.id, rule.detail))
    return violations


def scan_tree() -> list[Violation]:
    violations: list[Violation] = []
    for name in gui_sources():
        violations.extend(scan_text(name, (ROOT / name).read_text(encoding="utf-8", errors="replace")))
    return violations


def main() -> int:
    sources = gui_sources()
    if not sources:
        print("ERROR: GUI preflight-truth guard scanned no files", file=sys.stderr)
        return 1

    violations = scan_tree()
    if violations:
        print("ERROR: GUI preflight-truth guard failed:", file=sys.stderr)
        for violation in violations:
            print(violation.format(), file=sys.stderr)
        return 1

    print(
        f"GUI preflight-truth guard passed: {len(sources)} GUI file(s) render Core's preflight verdict "
        "and derive none of it."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
