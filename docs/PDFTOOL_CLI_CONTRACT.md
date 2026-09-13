# PdfTool CLI Result Contract

**Status:** adopted (issue #16)
**PdfTool automation schema version:** `1` (outer envelope)

Every PdfTool command that runs in JSON mode emits **exactly one JSON document to
stdout**. That document is the PdfTool result envelope. It versions **PdfTool's
automation contract**, which is separate from any domain report it carries
(preflight, OCR). Do not merge the two schemas.

The machine-facing contract is: branch on `status`, `exit_code`, and diagnostic
`code` — never on translated `message` text.

## Capability discovery

`PdfTool capabilities` is a JSON-first, versioned discovery command. It reports
what the current binary can actually invoke without opening documents, reading
user settings, launching sidecars, scanning directories, or making network
requests:

```text
PdfTool capabilities
PdfTool capabilities --command preflight
PdfTool capabilities --console-format json
```

The outer envelope keeps schema version `1`; the nested
`data.discovery_schema_version` is the independent discovery contract version.
Stable command IDs, option IDs/names, capability IDs, fixup IDs, and schema IDs
are locale-independent lower-case identifiers. Names and descriptions are for
display and may be translated. Arrays of machine identifiers are sorted by
stable ID. Consumers must ignore additive fields and increment handling when
`discovery_schema_version` changes.

Capability metadata marks password-bearing options such as `--pswd` and the
encryption password options as `sensitive: true`; discovery never includes
option values, local paths, environment variables, sidecar locations, or
secrets. Fixup metadata is sourced from the Core implementation registry and
contains only fixups available in the current build.

## Envelope

```json
{
  "schema_version": 1,
  "command": "preflight",
  "version": "0.2.0-alpha",
  "status": "findings",
  "exit_code": 1,
  "diagnostics": [
    {
      "severity": "warning",
      "code": "pdf.reader-repaired",
      "message": "The document contained recoverable structural errors.",
      "context": { "path": "example.pdf" }
    }
  ],
  "outputs": [
    {
      "kind": "file",
      "role": "primary",
      "path": "input-redacted.pdf",
      "state": "written"
    }
  ],
  "data": {
    "report": {
      "schema_version": 3,
      "pass": false,
      "profile": "Loop Default",
      "errors": [],
      "warnings": [],
      "fixups_available": []
    }
  }
}
```

The build-level fixup registry is available from `PdfTool capabilities
--console-format json` at `.data.fixups`. Preflight `fixups_available` is the
intersection of that implemented set with fixups present in the active profile
and applicable to the current finding/document. The Editor sidecar applies the
same registry filter, so CLI and Editor cannot advertise different build-level
fixup IDs.

| Field | Type | Meaning |
|---|---|---|
| `schema_version` | `integer` | PdfTool result-envelope schema version (`1`). Independent of nested `data.report.schema_version`. |
| `command` | `string` | Command that produced the result (e.g. `preflight`, `info`, `diff`). For a bare invocation this is `help`; for an unknown command it is the offending command string. |
| `version` | `string` | PdfTool application version (`QCoreApplication::applicationVersion()`). |
| `status` | `string` | Stable machine name for `exit_code`. See exit-code taxonomy. |
| `exit_code` | `integer` | Process exit code. `status` and the process exit code always agree. |
| `diagnostics` | `array` | Structured messages recorded during the run (warnings, handled errors). |
| `outputs` | `array` | Files that the command produced (or planned, under `--dry-run`). |
| `data` | `object` | Command-specific payload. Empty object when the command has none. |

## Exit-code taxonomy

| Exit | Enum | `status` | Meaning |
|---:|---|---|---|
| `0` | `Success` | `success` | Command completed and its domain result is clean |
| `1` | `Findings` | `findings` | Command completed correctly but found differences, validation failures, failed checks, etc. |
| `2` | `InvalidInvocation` | `invalid-invocation` | Bad/missing arguments or invalid command |
| `3` | `InputError` | `input-error` | Input missing, unreadable, bad password, corrupt beyond recovery |
| `4` | `ProcessingFailure` | `processing-failure` | Command started but operation could not complete |
| `5` | `PartialOutput` | `partial-output` | Some requested work/output succeeded, some failed |
| `6` | `Cancelled` | `cancelled` | User/signal cancellation |
| `7` | `InternalError` | `internal-error` | Unexpected invariant/internal failure |

Exit `1` does **not** mean the executable malfunctioned. In CI:

```bash
PdfTool diff old.pdf new.pdf --console-format json
```

- exit `1` (`findings`) means "valid comparison, differences found";
- exit `4` (`processing-failure`) means "the comparison could not be performed".

## Diagnostics

```json
{
  "severity": "warning",
  "code": "pdf.reader-repaired",
  "message": "The document contained recoverable structural errors.",
  "context": { "path": "example.pdf" }
}
```

- `severity`: `info` | `warning` | `error`.
- `code`: stable kebab-case identifier, e.g. `cli.invalid-arguments`,
  `cli.unknown-command`, `pdf.document-unreadable`, `pdf.invalid-password`,
  `pdf.reader-warning`, `output.already-exists`, `output.write-failed`,
  `output.empty-result`, `operation.cancelled`. Machine consumers branch on
  these codes; treat them as the stability contract. `message` is
  human-oriented and may change.
- `context`: optional free-form object (e.g. the offending path).

In JSON mode, handled errors and warnings are captured in `diagnostics` and are
**not** additionally written to stderr. In text/XML/HTML mode the existing
human-facing stderr behavior is preserved.

### Empty results

Extraction commands (`fetch-images`, `fetch-text`, `attachments`) complete
successfully when a document simply has nothing to extract - a vector-only page
has no images, a scanned page has no text. That case always records an
`output.empty-result` diagnostic whose `context` carries `subject` (what was not
produced) and `fail_if_empty` (whether the caller asked to fail on it):

```json
{
  "severity": "info",
  "code": "output.empty-result",
  "message": "No images were extracted from document 'vector-only.pdf'.",
  "context": { "subject": "images", "fail_if_empty": false }
}
```

By default the note is informational and the command still exits `0 success`,
because "this document has no figures" is a legitimate answer. Passing
`--fail-if-empty` raises the same diagnostic to `error` and exits
`1 findings`, so a pipeline that gates on "figures were produced" cannot be
green-lit by an empty output directory. The flag never changes which files are
written; it only chooses how an empty result is reported.

## Output records

```json
{
  "kind": "file",
  "role": "primary",
  "path": "output.pdf",
  "state": "written"
}
```

`state` values: `written` (file committed), `planned` (`--dry-run`), `partial`
(some requested files written, others failed). Multi-file operations (render,
separate, image extraction, attachment extraction) emit one record per produced
file. A run where some files succeed and some fail reports `partial-output`.

## JSON mode detection

`--console-format json` and `--console-format=json` are detected from the raw
command line before parsing, so that malformed command lines still return a
valid JSON error envelope when JSON was requested. The `preflight`, `ocr`, and
`capabilities` commands default to JSON because their contracts are
machine-readable; malformed invocations of those commands therefore also
return the envelope. Supplying a different console format to those commands is
an invalid invocation.

## Unknown command

An unknown command is an `invalid-invocation` (`exit_code` 2) with a
`cli.unknown-command` diagnostic. Human mode may still print help text, but the
process must no longer appear successful.

## Command data payloads

| Command family | JSON `data` | Typical non-zero clean execution |
|---|---|---|
| `help`, `info`, `info-*`, `statistics`, `fetch-text`, `ink-coverage` | Existing formatter tree | None |
| `diff` | Difference report | `1 findings` |
| `verify-signatures`, `verify-redaction` | Verification report | `1 findings` |
| `preflight` | `{ "report": <existing preflight report> }` | `1 findings` |
| `ocr` | `{ "report": <existing OCR report> }` | `5 partial-output` |
| `render`, `separate`, image/attachment extraction | Summary + `outputs[]` | `5 partial-output` |
| `optimize`, `redact`, encrypt/decrypt, unite, add-bleed | Operation data + final output | Usually `4 processing-failure` on failure |

## Schema

- Machine-readable envelope schema: `schemas/pdftool-result.schema.json`
  (versioned alongside this document).
- Preflight continues to validate against `loop-preflight/schemas/report.schema.json`
  (unchanged); the envelope carries it under `data.report`.
- OCR carries its report under `data.report` (unchanged domain schema).
