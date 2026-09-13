# Loop UI design system

Issue #194 / Linear MB-460. Defines the semantic tokens and the canonical
finding/check state mapping shared by every Loop surface, and records the
current adoption state.

## Naming note

Issue #194 was written against an earlier snapshot of this fork, before the
Qt Widgets GUI was retired and the product was renamed. It names paths under
`Pdf4QtLibGui/…` and `Pdf4QtEditorPlugins/…` and a namespace prefixed with
the product's previous name, none of which exist any more. This document and
the code it describes use the repository's current naming instead of the
issue's literal text: `pdfquick::tokens` in `LoopLibQuick`, `Loop`-prefixed
types, and this file at `docs/LOOP_DESIGN_SYSTEM.md`. `#193`, `#195`, `#196`,
and `#127` carry the same stale paths and will need the same translation when
they are picked up.

`docs/quick-design-tokens.json` (issue #178, ADR-007 P4-S5) already defined a
provisional colour/spacing/motion contract for the first Quick slice, checked
by `scripts/verify-quick-shell-policy.py`, and `LoopLibQuick/sources/canvaspalette.h`
already turns it into canvas overlay styling. This design system extends that
contract to a full semantic role set, typography, focus geometry, and every
non-canvas surface rather than replacing it. Dark-theme colour values below
match the JSON where a role existed there. `CanvasPalette` continues to own
canvas-specific stroke widths; typography, spacing, and focus *geometry* on
the canvas HUD now read the shared C++ tokens.

## Tokens

`LoopLibQuick/sources/looptokens.h`, namespace `pdfquick::tokens`.
`scripts/verify-quick-shell-policy.py` asserts the C++ spacing, typography,
focus, and density literals match `docs/quick-design-tokens.json`.

### Spacing

4px base grid, matching `docs/quick-design-tokens.json` `spacing.values_px`.

| Token | Value |
|---|---|
| `SpaceXs` | 4px |
| `SpaceS` | 8px |
| `SpaceM` | 12px |
| `SpaceL` | 16px |
| `SpaceXl` | 24px |
| `SpaceXxl` | 32px |

### Typography

| Token | Value | JSON field |
|---|---|---|
| `TypeBodyPx` | 14 | `typography.body_px` |
| `TypeSmallPx` | 12 | `typography.small_px` |
| `TypeHeadingPx` | 24 | `typography.heading_px` |

### Focus geometry

| Token | Value | JSON field |
|---|---|---|
| `FocusOutlineWidthPx` | 2 | `focus.outline_width_px` |
| `FocusOutlineOffsetPx` | 2 | `focus.outline_offset_px` |
| `MinimumPointerTargetPx` | 44 | `density.minimum_pointer_target_px` |
| `MinimumKeyboardTargetPx` | 32 | `density.minimum_keyboard_target_px` |

### Colour roles

Call sites name a `ColorRole` and a `LoopTheme`; `tokens::color(role, theme)`
resolves it. No call site outside `looptokens.cpp` hardcodes a colour.

`HighContrast` is a third theme, not a flag on `Dark`/`Light` — every role has
a value in all three. Its hue choices intentionally mirror
`CanvasPalette::highContrast()`.

Every pair below is a foreground role against the `SurfaceBase` background of
its theme, checked with the same relative-luminance contrast formula
`scripts/verify-quick-shell-policy.py` uses (WCAG 2.1): 4.5:1 minimum for text
roles, 3:1 minimum for icon/focus-ring/large-text roles. `TextDisabled` is
exempt per WCAG 1.4.3's disabled-content exception. The same minima are
asserted by `UnitTestsLoopStateVisual`.

| Role | Dark | Light | High contrast | Contrast (dark / light) |
|---|---|---|---|---|
| `SurfaceBase` | `#111827` | `#FFFFFF` | black | — |
| `SurfacePanel` | `#1F2937` | `#F1F5F9` | black | — |
| `SurfaceOverlay` | `#374151` | `#E2E8F0` | black | — |
| `TextPrimary` | `#F8FAFC` | `#0F172A` | white | 16.96:1 / 17.85:1 |
| `TextSecondary` | `#CBD5E1` | `#475569` | white | 11.95:1 / 7.58:1 |
| `TextDisabled` | `#64748B` | `#94A3B8` | white | exempt |
| `SeverityError` | `#FCA5A5` | `#B91C1C` | red | 9.35:1 / 6.47:1 |
| `SeverityWarning` | `#FCD34D` | `#B45309` | yellow | 12.30:1 / 5.02:1 |
| `SeverityInfo` | `#93C5FD` | `#1D4ED8` | cyan | 9.84:1 / 6.70:1 |
| `Success` | `#86EFAC` | `#15803D` | green | 12.63:1 / 5.02:1 |
| `StateIncomplete` | `#94A3B8` | `#475569` | white | 6.92:1 / 7.58:1 |
| `StateNotChecked` | `#64748B` | `#64748B` | white | 3.73:1 / 4.76:1 |
| `FocusRing` | `#C4B5FD` | `#6D28D9` | yellow | 9.61:1 / 7.10:1 |
| `DestructiveAction` | `#DC2626` | `#B91C1C` | `#B91C1C` | — (button fill; see below) |

`DestructiveAction` is a fill colour, not a foreground-on-`SurfaceBase` pair:
white text on `#DC2626` (dark) is 4.83:1; white text on `#B91C1C` (light and
high contrast) is 6.47:1; both are above the 4.5:1 text minimum. High-contrast
stroke red (`Qt::red`) is reserved for `SeverityError`; it is not used as a
button fill because white-on-`#FF0000` is only ~4.0:1.

`FocusRing` is deliberately a distinct hue (violet) from `SeverityWarning`
(amber) in both themes. `CanvasPalette` currently reuses one colour
(`m_focus`) for both the focus ring and warning-severity strokes; this is a
known divergence from the canonical roles, tracked as adoption work below
rather than changed here.

## The canonical state mapping

`LoopLibQuick/sources/loopstatevisual.h`, `pdfquick::tokens::resolveStateVisual()`.
One function, called by every surface; nothing else derives its own
presentation from `severity`, `PreflightCheckStatus::status`, or a decision's
kind.

| State | Source | Colour role | Icon | Accessible name | Never |
|---|---|---|---|---|---|
| Error | `PreflightFinding::severity == "error"` | `SeverityError` | filled circle | Error | — |
| Warning | `severity == "warning"` | `SeverityWarning` | filled triangle | Warning | — |
| Info | `severity == "info"` | `SeverityInfo` | filled square | Info | — |
| Incomplete | `PreflightCheckStatus::status != "ok"`, or a finding with an unrecognised severity string | `StateIncomplete` | hatched | Incomplete | **never green, never a checkmark** |
| Not checked | no finding, no status, and no active waiver for this revision | `StateNotChecked` | outline | Not checked | never green |
| Passed | `PreflightCheckStatus::status == "ok"`, no finding | `Success` | checkmark | Passed | — |
| Waived | an active `Waive` decision recorded against the finding | `SeverityWarning` + badge | badge overlay | Waived | **never the passed treatment** |

Non-colour cues are load-bearing: each `StateKind` has a unique `StateIcon`
and a unique `accessibleName`. Surfaces must expose the accessible name (or a
translation of it). When two states share greyscale luminance in a theme
(high-contrast Incomplete and NotChecked are both white), the icon and name
still distinguish them. `UnitTestsLoopStateVisual` asserts both uniqueness
and the greyscale-collision rule.

`resolveStateVisual(finding, status, decision, currentDocumentDigest, currentProfileDigest)`
takes three optional pointers plus the two digests `PreflightDecision::resolveState()`
needs to tell an active decision from a stale one (same shape as
`PreflightDecision::countsForSignoff()`, issue #126). Precedence, checked in
this order:

1. `decision` is a `Waive` and `decision->resolveState(...)` is `Active` →
   **Waived**, regardless of the finding's severity or the check's status.
2. Otherwise, `finding` is non-null → mapped by `severity`. An unrecognised
   severity string (something outside the `profile.schema.json` enum) takes
   the **Incomplete** treatment rather than being silently dropped or shown
   as a pass.
3. Otherwise, `status` is non-null → **Passed** only when `status == "ok"`;
   every other literal is **Incomplete**.
4. Otherwise → **Not checked**.

The two invariants this table exists to guarantee — an incomplete check never
renders as a pass, and a waived finding never renders as a pass — are
asserted by a table-driven test, `UnitTests/tst_loopstatevisualtest.cpp`
(`UnitTestsLoopStateVisual`), and mapped as architecture invariant I28.

`profile.schema.json`'s restriction-scoped statuses (`not_inspected`,
`not_applicable`) referenced by issue #194's original table belong to issue
#125, which is not yet implemented; `PreflightCheckStatus::status` today only
emits `ok`/`failed`/`warning`/`skipped`/`incomplete`/`unsupported`. All of
them already resolve correctly through rule 3 above (anything but `ok` is
Incomplete), so #125 landing a new status literal does not require a change
here — only a new named branch if a future surface wants a more specific
Incomplete presentation for it.

## Components

Not in this change. Finding card, inspector row, overlay, progress, empty,
error, and confirm implementations land with their consuming surfaces
(`#193`, `#195`, `#196`, `#127`) on `resolveStateVisual()` and the tokens above.

## Theme and high-DPI

Dark, light, and `HighContrast` are all defined above. `StateIcon` is drawn
with scene-graph/QML primitives (no bitmap assets); 100%/150%/200% scaling
is verified with the first consuming surface.

## Adoption

No Loop surface outside this design system consumes `resolveStateVisual()`
yet, because none of its consumers (`#193`, `#195`, `#196`, `#127`) have
landed. `CanvasPalette`'s existing severity-to-colour mapping
(`severityColor(OverlaySeverity)`) is the one place in the current codebase
that already does similar work; it is intentionally left as-is here (see the
`FocusRing`/`SeverityWarning` note above) and should be re-pointed at these
tokens when the canvas overlay work in `#196` picks it up, with its own
visual-regression coverage.

Typography (`TypeSmallPx`), spacing (`SpaceS`), and focus geometry
(`FocusOutlineWidthPx` / `FocusOutlineOffsetPx`) are already read by the
canvas HUD and overlay palette.

Issue #191 is closed; there is no open inherited-dialog manifest to extend.

## Integrated-candidate evidence

This change lands the shared token and mapping contract. Installed-candidate
evidence of product-surface adoption (keyboard/focus journeys, finding-card
visuals, visual regression of every component state) is gated on the semantic
consumers:

- B3 / GitHub #234 — verdict adoption
- B5 / GitHub #195 — preflight workflow
- B6 / GitHub #196 — finding navigation
- GitHub #127 — Inspector

Until those land, the proof for this issue is `UnitTestsLoopStateVisual`, the
token-alignment check in `scripts/verify-quick-shell-policy.py`, and
architecture invariant I28 — not an installed-package visual capture.
