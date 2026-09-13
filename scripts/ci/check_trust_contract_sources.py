#!/usr/bin/env python3
"""Audit the source-level semantic-trust boundaries.

This is intentionally a small contract check rather than a C++ parser. It
guards the names and seams that make the release gates reviewable: the
canonical preflight reducer, the single operation-history chain, and the
absence of a second JSONL/audit-event ledger. The unmanaged async launch
inventory is checked separately by check_unmanaged_async.py.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

REQUIRED_MARKERS = {
    "PdfTool/pdftoolpreflight.cpp": (
        "reducePreflightVerdict",
        "preflightVerdictProcessExitCode",
        "PDFOperationHistoryStore",
        "PDFOperationHistoryEvent",
    ),
    "PdfTool/pdftoolrepair.cpp": (
        "reducePreflightVerdict",
        "PDFOperationHistoryStore",
        "PDFOperationHistoryEvent",
    ),
    "LoopLibCore/sources/pdfpagemasterexport.cpp": ("reducePreflightVerdict", "preflightGateFailureMessage"),
    "LoopLibCore/sources/pdfpreflightverdict.h": ("reducePreflightVerdict",),
    "LoopLibCore/sources/pdfactionlist.cpp": ("reducePreflightVerdict", "applyCanonicalPreflightVerdict"),
    "LoopLibCore/sources/pdfstandardconversion.cpp": ("reducePreflightVerdict",),
    "LoopLibInteraction/sources/preflightcontroller.cpp": ("reducePreflightVerdict",),
    "LoopLibCore/sources/pdfoperationhistory.h": (
        "enum class PDFOperationHistoryEventKind",
        "struct LOOPLIBCORESHARED_EXPORT PDFOperationHistoryEvent",
    ),
    "LoopLibCore/sources/pdfoperationhistorystore.h": (
        "class LOOPLIBCORESHARED_EXPORT PDFOperationHistoryStore",
        "PDFOperationResult appendEvent(PDFOperationHistoryEvent event",
    ),
}

PRODUCT_ROOTS = (
    "PdfTool",
    "LoopLibCore",
    "LoopLibInteraction",
    "LoopEditor",
)
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".cc", ".cxx"}


def source_paths() -> list[Path]:
    paths: list[Path] = []
    for root_name in PRODUCT_ROOTS:
        root = ROOT / root_name
        if root.exists():
            paths.extend(path for path in root.rglob("*") if path.is_file() and path.suffix in SOURCE_SUFFIXES)
    return paths


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def main() -> int:
    failures: list[str] = []

    for raw_path, markers in REQUIRED_MARKERS.items():
        path = ROOT / raw_path
        if not path.is_file():
            failures.append(f"missing required source: {raw_path}")
            continue
        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in text:
                failures.append(f"{raw_path}: missing marker {marker!r}")

    for path in source_paths():
        path_name = relative(path)
        text = path.read_text(encoding="utf-8")
        if re.search(r"\bfindings\s*\.\s*isEmpty\s*\(\s*\)", text):
            failures.append(f"{path_name}: independent findings.isEmpty() verdict derivation")
        if path_name == "LoopLibInteraction/sources/preflightcontroller.cpp":
            if re.search(r"\berrors\s*\.\s*isEmpty\s*\(\s*\)", text):
                failures.append(f"{path_name}: independent errors.isEmpty() verdict derivation")
            if re.search(r"\bwarnings\s*\.\s*isEmpty\s*\(\s*\)", text):
                failures.append(f"{path_name}: independent warnings.isEmpty() verdict derivation")
        if re.search(r"\b(?:AuditEvent|AuditRecord|AuditEntry)\b", text):
            failures.append(f"{path_name}: second audit event type detected")
        if re.search(r"(?i)\.jsonl\b|\bjsonl\b", text):
            failures.append(f"{path_name}: JSONL provenance chain detected")

    if failures:
        print("ERROR: semantic-trust source audit failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("Semantic-trust source audit passed: canonical verdict and single history chain are intact.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
