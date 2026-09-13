# PDF/X and PDF/A conversion

Loop exposes standard conversion as the Core operation `standards-convert`.
PdfTool's `repair` command and PageMaster's headless export job call this same
operation. An Editor adapter is deferred until Loop's product GUI work clears
the S21 canvas / S22 Quick admission contracts described in
[`LOOP_SHELL_CONTRACT.md`](LOOP_SHELL_CONTRACT.md) — the 0.1.1 release gate
itself is already complete, so that document, not this one, is authoritative
on timing. No second conversion implementation is planned; PdfTool and
PageMaster already share the one Core implementation.

Supported targets are explicit: `PDF/X-1a:2001`, `PDF/X-3:2002`, `PDF/X-4`, and
`PDF/A-2b`. The selected target is recorded in the operation plan and report.
The report lists metadata, PDF version, output-intent, page-box, and optional
color-normalization and transparency-flattening changes before mutation.

Conversion is fail-closed. A CMYK ICC profile is required for PDF/X-1a and
PDF/X-3 normalization. Loop does not claim that fonts were embedded, actions
removed, or other unsupported constructs repaired when the Core implementation
cannot do so. Those findings remain blockers.

PDF/X-1a:2001 and PDF/X-3:2002 forbid live transparency, and `standards-convert`
flattens it for those two targets by default — and only for those two, since
PDF/X-4 and PDF/A-2b permit it. That default is the `flatten_transparency`
policy's `Automatic` value; the parameter is a three-state policy, so it
overrides the default in both directions (`false` opts out for X-1a/X-3, `true`
opts in for X-4) instead of being re-applied on top of it. Flattening rasterizes
affected page content — a real content change, reported under
`transparency_flatten` in the conversion report, never a silent approximation.

Every non-dry-run conversion requires an independent validator command. The
validator receives a temporary candidate through the `{input}` argument
placeholder. A zero exit status is necessary but not sufficient for PDF/X:
Loop also runs its own postflight policy and commits only when both pass. For
PDF/A-2b, the external validator is the conformance authority; Loop's own
metadata/output-intent work is not a PDF/A conformance claim.

Example:

```text
PdfTool repair input.pdf --operation standards-convert \
  --param target=PDF/X-4 \
  --param target_icc_base64=<base64-icc> \
  --param validator_program=verapdf \
  --param validator_arguments="validate --format text {input}" \
  --output output.pdf
```

No validator or Java runtime is bundled by default. Optional veraPDF/Temurin
packaging and licensing decisions are documented in
[`PACKAGING_LICENSING.md`](PACKAGING_LICENSING.md).

Independent validation is skip-if-missing in CI (`UnitTestsStandardOracle`
skips when `verapdf` is not on `PATH`). When a validator is configured, a
nonzero exit is a conversion **error**: no candidate is committed and Loop
never self-certifies PASS. Mock always-pass / always-fail / missing-program
tests cover that fail-closed table without bundling a JRE.

The platform qualification evidence contract for parser, signature, and
standards validators is documented in
[`INDEPENDENT_VALIDATION.md`](INDEPENDENT_VALIDATION.md). Missing external
tools remain `incomplete`, not PASS.

Synthetic conversion fixtures and the already-conformant / convertible /
unconvertible triad are described in
[`loop-preflight/testdata/conversion/README.md`](../loop-preflight/testdata/conversion/README.md).
Renderer differentials for color and overprint live in
[`RENDERER_DIFFERENTIALS.md`](RENDERER_DIFFERENTIALS.md).

## Fixture triad

`loop-preflight/testdata/conversion/manifest.json` names three cases:

| Kind | Meaning |
|------|---------|
| already-conformant | Structural stand-in; still requires an independent validator |
| safely-convertible | Metadata Loop can rewrite when a validator is configured |
| deliberately-unconvertible | Oracle mismatch must remain ERROR, never PASS |

`UnitTestsConversionOracle` proves a missing oracle cannot self-certify and that
`/bin/false` (or a failing veraPDF) is ERROR. The veraPDF lane is skip-if-missing.
