Category: added
Audience: developers, print-production operators
Breaking-Change: no
Summary: Wire the existing PDFTransparencyFlattener operation (issue #164) into
the standards-convert Core operation (issue #167) so PDF/X-1a:2001 and
PDF/X-3:2002 conversion no longer treats live transparency as an unconditional,
unfixable blocker. Flattening runs by default for those two targets (which
prohibit live transparency) before the output-intent and page-box rewrite, is
reported as a real content change under a new transparency_flatten report
field (never a silent approximation), and remains skippable/enable-able via a
new flatten_transparency parameter surfaced identically through PdfTool's
repair command and PageMaster's export job (the one shared implementation).
PDF/X-4 and PDF/A-2b, which permit live transparency, do not flatten by
default. Also correct docs/STANDARD_CONVERSION.md's stale claim that an Editor
adapter can land "after the 0.1.1 GUI gate" — that gate is already complete
per docs/LOOP_SHELL_CONTRACT.md; Editor integration actually remains deferred
behind the still-closed S21/S22 product-GUI admission contracts, and
docs/REPO_MAP.md's LoopEditorPlugins/ module does not exist in the current
Qt-Quick-based tree, so a future Editor adapter belongs under
LoopLibInteraction/ + LoopEditor/qml/ instead.
