#!/usr/bin/env python3
"""Validate the static admission contracts for the staged Qt Quick shell."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
POLICY_PATH = ROOT / "docs" / "quick-shell-policy.json"
TOKENS_PATH = ROOT / "docs" / "quick-design-tokens.json"
EXCLUDED_DIRECTORIES = {
    ".git",
    ".codegraph",
    ".claude",
    ".docker-vcpkg",
    ".worktrees",
    "build",
    "loop",
    "node_modules",
    "vcpkg",
    "vcpkg_installed",
}
REQUIRED_IMPORTS = {
    "QtQuick",
    "QtQuick.Controls",
    "QtQuick.Layouts",
    "QtQuick.Window",
}
COLOR_PATTERN = re.compile(r"^#[0-9a-fA-F]{6}$")
IMPORT_PATTERN = re.compile(
    r"^\s*import\s+([A-Za-z_][A-Za-z0-9_.]*)(?:\s+\d+(?:\.\d+)?)?(?:\s+as\s+[A-Za-z_][A-Za-z0-9_]*)?\s*$",
    re.MULTILINE,
)


class ContractError(ValueError):
    """Raised when a static Quick contract is malformed or violated."""


def load_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(f"cannot read {path.relative_to(ROOT)}: {exc}") from exc
    if not isinstance(value, dict):
        raise ContractError(f"{path.relative_to(ROOT)} must contain a JSON object")
    return value


def require_number(value: object, label: str, *, minimum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ContractError(f"{label} must be numeric")
    number = float(value)
    if minimum is not None and number < minimum:
        raise ContractError(f"{label} must be at least {minimum}, got {number}")
    return number


def channel(value: int) -> float:
    value = value / 255.0
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def luminance(color: str) -> float:
    if not COLOR_PATTERN.fullmatch(color):
        raise ContractError(f"invalid token color {color!r}; expected #RRGGBB")
    red, green, blue = (int(color[index : index + 2], 16) for index in (1, 3, 5))
    return 0.2126 * channel(red) + 0.7152 * channel(green) + 0.0722 * channel(blue)


def contrast_ratio(first: str, second: str) -> float:
    first_luminance = luminance(first)
    second_luminance = luminance(second)
    light = max(first_luminance, second_luminance)
    dark = min(first_luminance, second_luminance)
    return (light + 0.05) / (dark + 0.05)


def validate_policy(policy: dict) -> None:
    if policy.get("schema_version") != 1 or policy.get("issue") != 178:
        raise ContractError("quick-shell-policy must use schema_version=1 and issue=178")
    if policy.get("status") != "admission-contract":
        raise ContractError("quick-shell-policy status must be admission-contract")

    imports = policy.get("qml_imports")
    if not isinstance(imports, dict):
        raise ContractError("quick-shell-policy qml_imports must be an object")
    allowed = imports.get("allowed")
    if not isinstance(allowed, list) or not all(isinstance(item, str) for item in allowed):
        raise ContractError("quick-shell-policy allowed imports must be a string list")
    missing_imports = sorted(REQUIRED_IMPORTS - set(allowed))
    if missing_imports:
        raise ContractError(f"quick-shell-policy is missing imports: {', '.join(missing_imports)}")

    forbidden_patterns = policy.get("forbidden_qml_patterns")
    if not isinstance(forbidden_patterns, list) or not forbidden_patterns:
        raise ContractError("quick-shell-policy must define forbidden_qml_patterns")
    for pattern in forbidden_patterns:
        if not isinstance(pattern, str):
            raise ContractError("every forbidden QML pattern must be a string")
        try:
            re.compile(pattern, re.IGNORECASE)
        except re.error as exc:
            raise ContractError(f"invalid forbidden QML pattern {pattern!r}: {exc}") from exc

    boundaries = policy.get("runtime_boundaries")
    if not isinstance(boundaries, dict):
        raise ContractError("quick-shell-policy runtime_boundaries must be an object")
    for key in (
        "network_access",
        "filesystem_access",
        "process_launch",
        "remote_imports",
        "customer_content_persistence",
        "pdf_payload_logging",
    ):
        if boundaries.get(key) is not False:
            raise ContractError(f"quick-shell-policy must deny runtime boundary {key}")

    accessibility = policy.get("accessibility_contract")
    if not isinstance(accessibility, list) or len(accessibility) < 8:
        raise ContractError("quick-shell-policy accessibility contract is incomplete")

    verification = policy.get("verification")
    if not isinstance(verification, dict):
        raise ContractError("quick-shell-policy verification must be an object")
    if verification.get("preferred_rhi_variable") != "QSG_RHI_BACKEND":
        raise ContractError("Quick preferred-RHI evidence must use QSG_RHI_BACKEND")
    if verification.get("software_backend_variable") != "QT_QUICK_BACKEND=software":
        raise ContractError("Quick software evidence must use QT_QUICK_BACKEND=software")


def validate_tokens(tokens: dict) -> None:
    if tokens.get("schema_version") != 1 or tokens.get("issue") != 178:
        raise ContractError("quick-design-tokens must use schema_version=1 and issue=178")
    if tokens.get("status") != "provisional-admission-contract":
        raise ContractError("quick-design-tokens status must be provisional-admission-contract")

    colors = tokens.get("colors")
    if not isinstance(colors, dict):
        raise ContractError("quick-design-tokens colors must be an object")
    for name, value in colors.items():
        if not isinstance(value, str) or not COLOR_PATTERN.fullmatch(value):
            raise ContractError(f"quick-design-tokens color {name} must be #RRGGBB")

    pairs = tokens.get("contrast_pairs")
    if not isinstance(pairs, list) or not pairs:
        raise ContractError("quick-design-tokens must define contrast_pairs")
    for pair in pairs:
        if not isinstance(pair, dict):
            raise ContractError("every contrast pair must be an object")
        foreground = pair.get("foreground")
        background = pair.get("background")
        if foreground not in colors or background not in colors:
            raise ContractError(f"contrast pair {pair.get('name', '<unnamed>')} references an unknown color")
        minimum = require_number(pair.get("minimum_ratio"), f"contrast pair {pair.get('name', '<unnamed>')} minimum_ratio", minimum=3.0)
        actual = contrast_ratio(colors[foreground], colors[background])
        if actual < minimum:
            raise ContractError(
                f"contrast pair {pair.get('name', '<unnamed>')} is {actual:.2f}, below {minimum:.2f}"
            )

    typography = tokens.get("typography")
    spacing = tokens.get("spacing")
    density = tokens.get("density")
    focus = tokens.get("focus")
    motion = tokens.get("motion")
    if not all(isinstance(value, dict) for value in (typography, spacing, density, focus, motion)):
        raise ContractError("typography, spacing, density, focus, and motion must be objects")
    require_number(typography["body_px"], "typography.body_px", minimum=12)
    require_number(typography["small_px"], "typography.small_px", minimum=10)
    require_number(typography["minimum_text_scale"], "typography.minimum_text_scale", minimum=1)
    if typography.get("supports_user_scaling") is not True:
        raise ContractError("typography must support user scaling")

    spacing_values = spacing.get("values_px")
    if not isinstance(spacing_values, list) or spacing_values != sorted(spacing_values):
        raise ContractError("spacing.values_px must be an ordered list")
    if len(spacing_values) < 4 or any(require_number(value, "spacing value", minimum=0) < 0 for value in spacing_values):
        raise ContractError("spacing.values_px must contain at least four non-negative values")
    require_number(spacing["unit_px"], "spacing.unit_px", minimum=1)
    require_number(density["minimum_pointer_target_px"], "density.minimum_pointer_target_px", minimum=44)
    require_number(focus["outline_width_px"], "focus.outline_width_px", minimum=2)
    require_number(focus["minimum_non_text_contrast_ratio"], "focus.minimum_non_text_contrast_ratio", minimum=3)
    require_number(motion["maximum_duration_ms"], "motion.maximum_duration_ms", minimum=0)
    if motion.get("supports_reduced_motion") is not True or motion.get("reduced_motion_duration_ms") != 0:
        raise ContractError("motion must define a zero-duration reduced-motion mode")


def require_cpp_constexpr_int(source: str, name: str, expected: int, *, relative: str) -> None:
    match = re.search(rf"inline constexpr int {re.escape(name)} = (\d+);", source)
    if not match:
        raise ContractError(f"{relative} is missing inline constexpr int {name}")
    actual = int(match.group(1))
    if actual != expected:
        raise ContractError(f"{relative} {name} is {actual}, expected {expected} from docs/quick-design-tokens.json")


def validate_cpp_tokens(root: Path, tokens: dict) -> None:
    relative = "LoopLibQuick/sources/looptokens.h"
    header = root / relative
    try:
        source = header.read_text(encoding="utf-8")
    except OSError as exc:
        raise ContractError(f"cannot read {relative}: {exc}") from exc

    spacing_values = tokens["spacing"]["values_px"]
    named_spacing = {
        "SpaceXs": 4,
        "SpaceS": 8,
        "SpaceM": 12,
        "SpaceL": 16,
        "SpaceXl": 24,
        "SpaceXxl": 32,
    }
    for name, expected in named_spacing.items():
        if expected not in spacing_values:
            raise ContractError(f"docs/quick-design-tokens.json spacing.values_px is missing {expected}")
        require_cpp_constexpr_int(source, name, expected, relative=relative)

    typography = tokens["typography"]
    require_cpp_constexpr_int(source, "TypeBodyPx", int(typography["body_px"]), relative=relative)
    require_cpp_constexpr_int(source, "TypeSmallPx", int(typography["small_px"]), relative=relative)
    require_cpp_constexpr_int(source, "TypeHeadingPx", int(typography["heading_px"]), relative=relative)

    focus = tokens["focus"]
    require_cpp_constexpr_int(source, "FocusOutlineWidthPx", int(focus["outline_width_px"]), relative=relative)
    require_cpp_constexpr_int(source, "FocusOutlineOffsetPx", int(focus["outline_offset_px"]), relative=relative)

    density = tokens["density"]
    require_cpp_constexpr_int(source, "MinimumPointerTargetPx", int(density["minimum_pointer_target_px"]), relative=relative)
    require_cpp_constexpr_int(source, "MinimumKeyboardTargetPx", int(density["minimum_keyboard_target_px"]), relative=relative)


def qml_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*.qml"):
        relative = path.relative_to(root)
        if any(directory in EXCLUDED_DIRECTORIES for directory in relative.parts):
            continue
        files.append(path)
    return sorted(files)


def validate_qml_sources(root: Path, policy: dict) -> list[str]:
    errors: list[str] = []
    imports = set(policy["qml_imports"]["allowed"])
    patterns = [re.compile(pattern, re.IGNORECASE) for pattern in policy["forbidden_qml_patterns"]]
    for path in qml_files(root):
        relative = path.relative_to(root)
        try:
            source = path.read_text(encoding="utf-8")
        except OSError as exc:
            errors.append(f"{relative}: cannot read QML source: {exc}")
            continue
        for match in IMPORT_PATTERN.finditer(source):
            module = match.group(1)
            if module not in imports:
                line = source.count("\n", 0, match.start()) + 1
                errors.append(f"{relative}:{line}: QML import {module!r} is not allowlisted")
        for pattern in patterns:
            match = pattern.search(source)
            if match:
                line = source.count("\n", 0, match.start()) + 1
                errors.append(f"{relative}:{line}: forbidden QML pattern {pattern.pattern!r}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root to validate")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        policy = load_json(root / "docs" / "quick-shell-policy.json")
        tokens = load_json(root / "docs" / "quick-design-tokens.json")
        validate_policy(policy)
        validate_tokens(tokens)
        validate_cpp_tokens(root, tokens)
        errors = validate_qml_sources(root, policy)
        if errors:
            raise ContractError("\n".join(errors))
    except ContractError as exc:
        print(f"Quick shell policy FAILED: {exc}", file=sys.stderr)
        return 1

    count = len(qml_files(root))
    print(f"Quick shell policy verified: imports, boundaries, tokens, accessibility contract; qml_files={count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
