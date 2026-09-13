# Canonical preflight verdict

Loop reduces every normalized `PreflightResult` through
`pdf::reducePreflightVerdict()` in Core. The reducer is the only authority for
the operator-facing outcome; the legacy `pass` boolean is emitted only as a
derived compatibility field.

The terminal states are:

| State | Meaning | PdfTool exit code |
| --- | --- | ---: |
| `pass` | Inspection completed and no unwaived blocking finding remains | 0 |
| `fail` | Inspection established at least one blocking finding | 1 |
| `incomplete` | Required evidence was not inspected, including budget exhaustion | 8 |
| `error` | The document, profile, or engine operation failed | 9 |

`preflightVerdictProcessExitCode()` is the shared mapping those exits use.
Reports carry a `verdict` object with the state, machine-readable
`reason_code`, human-readable reason, and the blocking/waived stable finding
IDs. A budget finding is evidence that inspection could not finish; it is not a
blocking finding by itself. This prevents a raster budget exhaustion with zero
findings from being reported as a clean pass.

Certificate issuance (#133) may proceed only when
`PreflightVerdict::allowsCertificateIssuance()` is true (PASS). Incomplete,
fail, and error must not produce `CertificateIssued`.

Editor copy uses `preflightVerdictOperatorSummary()` so operators see "No
problems found." versus "Could not finish inspecting." The Editor's findings
model carries the verdict's waived finding IDs, so a waived finding keeps its
place in the list — marked waived, and presented as non-blocking on the canvas —
instead of contradicting the PASS verdict. PageMaster gates use
`preflightGateFailureMessage()` so Incomplete is not labeled as a generic fail.
Action List step results store the canonical `verdict` object and fail-close
postflight that is not PASS via `applyCanonicalPreflightVerdict()`.

Core, PdfTool, PageMaster export, repair postflight, standard-conversion
postflight, Action List step results, the Editor controller, and the
certificate gate consume this same contract. New surfaces must call the Core
reducer or consume the normalized `verdict` object; they must not infer status
from `errors.isEmpty()` or `findings.isEmpty()`.
