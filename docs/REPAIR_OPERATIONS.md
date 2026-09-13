# Loop repair operations

Loop corrective editing is represented by one Core contract shared by the
Editor and PdfTool. An operation is first analyzed against an immutable source
document, producing a serializable plan. The plan names its operation version,
typed parameters, risk, affected semantic domains and targets, preconditions,
expected changes, warnings and required validators.

## Transaction lifecycle

`PDFRepairTransaction` owns the lifecycle:

1. `add()` composes registered operations in a deterministic order.
2. `analyze()` produces plans without modifying the source document.
3. `apply()` copies the source into an isolated candidate and applies every
   planned operation. A failed or cancelled operation discards the candidate.
4. `serializeCandidate()` writes a candidate through the shared safe writer and
   reopens it, so the reviewed artifact is the artifact that can be committed.
5. `compareCandidate()` invokes the #28 deterministic structural and visual
   diff engine with the plan's expected changes and affected pages.
6. The caller may run the normal preflight profile against the reopened
   candidate. Incomplete or unexpected evidence never becomes a final output.

The source document is never mutated by analysis or by a failed transaction.
Final output publication is an atomic `PDFSafeFileWriter` write followed by a
byte-for-byte and reopen verification.

## Built-in operations

The first adapters use the existing bounded Core fixups:

- `add-bleed` - page geometry and generated edge-extension content.
- `downsample-images` - image resource optimization with target DPI and quality
  parameters.
- `rgb-to-cmyk` - ICC-managed color conversion and output-intent handling; a
  target ICC profile is required and unsupported constructs are reported rather
  than silently approximated.
- `standards-convert` - shared PDF/X and PDF/A conversion for
  `PDF/X-1a:2001`, `PDF/X-3:2002`, `PDF/X-4`, and `PDF/A-2b`. It produces a
  pre-conversion change report and refuses to commit unless an explicitly
  configured independent validator accepts the candidate. Validator arguments
  must include `{input}`. Transparency flattening (via the shared
  `PDFTransparencyFlattener` operation, default-on for X-1a/X-3) is a reported
  content change, not a silent approximation; font embedding and
  forbidden-action removal remain unsupported and fail closed.

The preflight capability list is derived from the same registry: an operation
is advertised only when its descriptor marks it as a preflight fixup. Profile
validation rejects unknown or unimplemented fixup IDs, so the report cannot
advertise a remedy that the registered repair contract cannot execute.
The fixup capability registry is the source of truth for the current build. The
shipped Loop Default profile lists these registered IDs, and the Editor sidecar
filter plus `PdfTool capabilities --console-format json` consume that registry.
An operation is advertised only when it is implemented in the current build, is
applicable to the raised finding/document, and is present in the active profile.
Use the following command to inspect the build-level set before writing an
operator runbook or automation policy:

```bash
PdfTool capabilities --console-format json | jq '.data.fixups'
```

The registry exposes repair descriptors through `PdfTool repair --list-operations`.
The Editor's preflight workflows resolve `add-bleed`, `downsample-images`, and
`rgb-to-cmyk` from that registry and review a separate serialized candidate
before writing it. PdfTool's `repair` command uses the same transaction contract.

## PdfTool contract

Example:

```text
PdfTool repair input.pdf --operation add-bleed \
  --param mode=mirror --param bleed_mm=3 \
  --output output.pdf --report-file repair.json --overwrite \
  --profile loop-default.json
```

`--dry-run` emits the plan without writing a candidate. `--render-dir` stores
bounded diff artifacts. `--allow-incomplete` permits an incomplete report to
be returned for operator review, but it never authorizes committing that
candidate. A `--profile` is required for commit; without it, the report records
that normal postflight was not run and the command returns without writing.

Risky operations remain explicit in their plan and descriptor. Approval belongs
to the surface invoking the transaction: PdfTool is unattended and must be
given an output path, while Editor presents the serialized preview and requires
operator acceptance before its atomic write.
