# V1 release readiness audit

Audit date: **2026-07-23**, revised **2026-07-24**, corrected **2026-07-25**, signing gate struck **2026-08-02**, gates 1–4 evidence pass **2026-08-03**, fuzz evidence returned **2026-08-03**
Product: **Loop PDF 0.1.0-alpha** (Qt6 desktop PDF toolkit)
Scope: operational, security, reliability, data-integrity, compatibility, and release-readiness for first public launch.
Platforms: **Windows and Linux** (macOS is not a V1 platform — see `docs/PLATFORM_SUPPORT.md`).

## Executive recommendation

**Ready for product-owner sign-off (step 5).** Every launch gate is green on `master`.
The fuzz gate — the last one outstanding — returned two real JBIG2 findings. The first fix
(PR #63) closed the overflow but bounded only composited pixels, which the pinned timeout
seed never reached; PR #65 replaced it with the two-budget `accountDecodeWork` and added a
regression corpus. The gate is green on the commit containing both.

| Gate | Status | Evidence (2026-08-03) |
|------|--------|------------------------|
| `ci.yml` build + `ctest` | **Pass** | [Run 30792705447](https://github.com/mberrys/Loop-pdf/actions/runs/30792705447) on `master` |
| Fuzz on `master` (MIC-326) | **Pass** | [Run 30937285025](https://github.com/mberrys/Loop-pdf/actions/runs/30937285025) — green on `9ed6a8e2` at the full 600 s/target budget. That commit contains both PR #63 (overflow + first budget) and PR #65 (`accountDecodeWork` + regression corpus), which together close the two findings from [run 30803378370](https://github.com/mberrys/Loop-pdf/actions/runs/30803378370) |
| Windows MSI smoke (MIC-301 / MIC-327) | **Pass** | [Run 30792705392](https://github.com/mberrys/Loop-pdf/actions/runs/30792705392) — green after the WiX ICU/harvest fixes (`7127f65`, `29b553f`) |
| Linux AppImage smoke (MIC-301) | **Pass** | [Run 30787629154](https://github.com/mberrys/Loop-pdf/actions/runs/30787629154) with `scripts/smoke-test-appimage.sh` |
| Overprint disclosure (MIC-330) | **Pass** | `overprintDisclosureText()` always shown; README + runbook R-002; `tst_operatoracceptance.cpp` (`overprintDisclosureText_alwaysShownEvenWithoutFinding`, `overprintDisclosureText_addsSpecificWarningForWhiteOverprintFinding`). The additional canvas fidelity indicator remains pending PR #395 and is not counted in the current-state evidence — see §3 R-002 |
| Unsigned installer disclosure (MIC-342) | **Pass** | README Install section + runbook R-016 + `SHA256SUMS.txt` via `CreateReleaseDraft.yml` |

No launch-blocking gate remains open. The next step is the owner review in §8.

**V1 / 1.0 ships unsigned** (reaffirmed 2026-08-02). Authenticode / DigiCert / `SIGN_MSI` are post-V1 / paid only ([MIC-345](https://linear.app/mbx2/issue/MIC-345)).

The 2026-07-23 revision concluded "ready with explicit risks." The 2026-07-24 revision
concluded "not ready — one launch-blocking defect." **Both were wrong.** This revision
supersedes them.

### R-000 was already fixed when it was reported

The 2026-07-24 revision stated that the V1 operator loop did not work on `master` because
the engine emitted `schema_version: 3` while the plugin validator capped at `2`, and that
*"the fix exists on the `Pre-P3-sanitize` branch (PR #54) and is unmerged."*

That was not true at the time of writing, and is not true now:

- `LoopLibCore/sources/preflightengine.h:52` → `PREFLIGHT_REPORT_SCHEMA_VERSION = 3`
- `UnitTests/support/preflight/preflightsidecarutils.h:42` →
  `LOOP_PREFLIGHT_SCHEMA_VERSION 3`, with `isSupportedSchemaVersion` accepting 1–3
- The fix is commit `c515bfa3`, merged to `master` via `ba428f1b` (PR #54) at
  2026-07-24 10:58 PDT
- The revision asserting it was unmerged is commit `d43a70ec` (PR #56) at 11:59 PDT —
  **one hour later**, with `c515bfa3` already in its own history
  (`git merge-base --is-ancestor c515bfa3 d43a70ec` succeeds)

R-000 is struck. Because checklist item A11 attributed all CI redness to R-000, current CI
status must be re-derived rather than inherited from this document.

### Launch gates

| ID | Risk | Status | Linear | Mitigation required |
|----|------|--------|--------|---------------------|
| ~~R-000~~ | ~~Preflight report schema contract broken on `master`~~ | **Struck 2026-07-25** | — | Not a defect. Engine and validator are both at schema 3 on `master` |
| ~~R-003~~ | ~~JBIG2 decoder: signed-integer overflow + unbounded decode (DoS)~~ | **Pass 2026-08-04** | [MIC-326](https://linear.app/mbx2/issue/MIC-326) | Fuzz CI now runs and found two real defects on the first workflow run that executed. Both fixed in `pdfjbig2decoder.cpp` (PR #63, completed by PR #65; see §3 R-003 and §4). [Run 30937285025](https://github.com/mberrys/Loop-pdf/actions/runs/30937285025) is green on `master` at the full budget — MIC-326 can be closed |
| ~~R-001~~ | ~~MIC-301 — installer / clean-machine validation~~ | **Pass 2026-08-03** | [MIC-301](https://linear.app/mbx2/issue/MIC-301), [MIC-327](https://linear.app/mbx2/issue/MIC-327) | Windows: `Invoke-MsiSmokeTest.ps1` in `WindowsInstall.yml` — [Run 30792705392](https://github.com/mberrys/Loop-pdf/actions/runs/30792705392) green. Linux: `smoke-test-appimage.sh` in `LinuxInstall.yml` — [Run 30787629154](https://github.com/mberrys/Loop-pdf/actions/runs/30787629154) green. `.deb` remains out of scope (broken; not on releases) |
| ~~R-016~~ | ~~V1 MSI ships unsigned; no certificate held~~ | **Not a V1 gate — reaffirmed 2026-08-02** | [MIC-342](https://linear.app/mbx2/issue/MIC-342) disclosure · [MIC-345](https://linear.app/mbx2/issue/MIC-345) procurement (post-V1) | **Ship unsigned for 1.0.** Signing never blocks launch. Disclosure (SmartScreen + `SHA256SUMS.txt`) is a marketing/docs obligation in **V1 release documentation**, not an engineering gate. Procurement stays in **Commercial / paid distribution (post-V1)** |
| **R-002** | MIC-320 — overprint not simulated in standard page rendering | **Accepted — mitigation complete** | [MIC-330](https://linear.app/mbx2/issue/MIC-330) | Documented limitation + in-app disclosure. **Preflight**: the report panel shows the general "page view does not simulate overprint" notice unconditionally once a report is loaded, with an additional specific warning appended only when a `white-overprint` finding is present. **Canvas**: the persistent fidelity indicator and opt-in authoritative page render are pending PR #395; do not credit them in the current-state audit until that PR is merged into `dev`. The default canvas render remains approximate, so this does not reopen MIC-320's deferral. Deferral of MIC-320 is recorded in Linear (project description + MIC-306), not only here — see §3 |
| ~~R-015~~ | ~~macOS declared supported without CI, packaging or notarization~~ | **Resolved** | [MIC-336](https://linear.app/mbx2/issue/MIC-336) | macOS restated as post-V1; V1 ships Windows + Linux. MIC-336 reopened — it had been closed Done with its acceptance unmet |

Two audit items previously listed as gaps were already built and only needed correcting:
`scripts/smoke-test-install.ps1` (most of MIC-301's functional checks) and
`scripts/generate-third-party-notices.ps1`.

### Product decisions — 2026-07-25

Recorded in the Linear project description, which is the authority. Reproduced here so this
document cannot drift from it again.

| Question | Decision |
|----------|----------|
| Overprint-correct rendering a V1 gate? | **No** — deferred post-V1 (MIC-320). V1 ships detection + disclosure, no overprint-safe claim. PR #395 proposes an opt-in, one-click accurate render of the current page, but that capability remains pending until it merges into `dev`; the default render is approximate, so this does not reopen the deferral |
| Sign the Windows installer? | **No** — V1 / 1.0 ships unsigned (reaffirmed 2026-08-02). Procurement is post-V1 (MIC-345). Signing is **not** a launch blocker |
| Linux a V1 platform? | **Yes** — Windows and Linux both ship V1. macOS post-V1 |
| OCR in V1? | **CLI-only** — `PdfTool ocr` only; no `OcrPlugin`, no bundled sidecar service (MIC-343) |
| V1 free or paid? | **Free public release** — licensing checklist gates first paid distribution |

The CLI-only OCR decision has a packaging consequence worth stating plainly: `PdfTool ocr`
ships but is **inert without a sidecar**, and no sidecar is bundled. OCR in V1 is
bring-your-own and unsupported. This keeps the "no OCR service required" property in the
product brief true, and keeps a Python/PyInstaller bundle out of the V1 attack surface.

> **Authority note (added 2026-07-25).** This document does **not** set the V1 gate list.
> The Linear project description does. The 2026-07-24 revision reclassified MIC-320 from
> launch gate to "Accepted" while the project description, roadmap, product brief, and
> MIC-306 all still called it a hard gate — leaving two live, contradictory definitions of
> V1 for two days. Both now agree that MIC-320 is deferred. Any future change to a gate
> must amend the project description in the same change.

No web SaaS, accounts, or payments exist — many classic launch checklist items are **N/A** (see §Not applicable).

**Commercial status:** V1 is a **free public release**. The MIC-140 / MIC-329 licensing
checklist (artifact SBOM, Qt LGPL relink evidence, counsel sign-off) therefore gates the
first *paid* distribution, not this launch. See §5 and `docs/PACKAGING_LICENSING.md`.

---

## 1. Release surface inventory

### User roles

| Role | Surface | Notes |
|------|---------|-------|
| **Operator** | LoopEditor (Quick shell; preflight is in-process since the widget plugin retired) | Primary V1 sellable loop |
| **Automation / CI** | PdfTool CLI | `preflight`, `add-bleed`, `ocr` (optional) |
| **Power user** | PageMaster, Diff, Viewer, LaunchPad | Adjacent; not V1 contract |
| **Maintainer** | GitHub Actions, packaging scripts | Release engineering |

There are **no** tenant roles, admin consoles, or hosted user accounts.

### Environments and deployment targets

| Target | Mechanism | Path |
|--------|-----------|------|
| Linux CI | `.github/workflows/ci.yml` | Ubuntu build + `ctest` + `.deb` artifact (non-PR events only; builds but doesn't run — verified 2026-08-02, see §3 R-001) |
| Windows CI | `.github/workflows/ci.yml` | Build + zip artifact |
| Windows MSI | `.github/workflows/WindowsInstall.yml` | `WixInstaller/` |
| Linux AppImage | `.github/workflows/LinuxInstall.yml` | Manual dispatch |
| Linux Flatpak | `.github/workflows/LinuxFlatpak.yml` | `Flatpak/io.github.mberrys.Loop-pdf.json` |
| Draft release | `.github/workflows/CreateReleaseDraft.yml` | Aggregates AppImage + MSI |

**Not in CI:** macOS builds.

### External services (optional)

| Service | Required? | Opt-in mechanism |
|---------|-----------|-------------------|
| **Sentry** crash telemetry | Yes (Windows) | Compile-time DSN; `SENTRY_DSN=off` to disable; `LOOP_ENABLE_SENTRY` |
| **OCR Python sidecar** | No | `LOOP_OCR_SIDECAR` / bundled `LoopOcrService` |
| **GitHub / Sponsor links** | No | `QDesktopServices::openUrl` from Help menu only |

No payment processors, identity providers, or document cloud APIs.

### Launch-critical dependencies

| Dependency | SPOF? | Fallback |
|------------|-------|----------|
| Qt 6.11.1 runtime | Yes | User must install/bundle Qt (installers do) |
| LoopLibCore PDF engine | Yes | None — core product |
| Bundled `PdfTool` + `loop-default.json` | Yes for Editor preflight | Actionable error if missing from bundle |
| vcpkg third-party libs (OpenJPEG, zlib, …) | Build-time | Static link in release builds |
| Tesseract/EasyOCR (OCR only) | Yes for OCR feature | OCR disabled if sidecar missing |

### PDF data lifecycle

```
Open/import (local file) → in-memory PDFDocument → process (render, preflight, fixup)
  → export/save-as (atomic QSaveFile) → user filesystem
Temp: QTemporaryDir for preflight snapshots, OCR page PNGs, attachment extract
Deletion: user deletes files; temp dirs removed on scope exit; sanitize strips metadata/attachments on request
Retention: none server-side; Sentry may retain crash minidumps if enabled. Minidumps are
  captured by crashpad and include thread stacks and referenced heap memory, so they CAN
  contain PDF content and file paths. Nothing in the SDK can scrub them — see R-008.
Logging: PdfTool and Editor both write a rotating, privacy-scrubbed log file via
  pdf::PDFLogSession (2 MiB cap, 3 files kept); PdfTool's stderr/stdout is unchanged,
  the log handler chains to it. Scrubbing (home/temp dir, login name, host name, other
  absolute paths with the basename dropped, email addresses, IPv4/IPv6 literals) happens once,
  in the log sink, not at each call site. `PdfTool diagnostics` / Editor Help > Collect
  Diagnostics build a bundle from the logs plus non-secret runtime metadata - no PDF content,
  no recent-files list, no crash minidumps (see R-008 above; a bundle is not a substitute
  for the same scrubbing guarantee on minidumps, which does not exist).
```

---

## 2. V1 release-readiness checklist

| # | Area | Check | Status | Evidence |
|---|------|-------|--------|----------|
| A1 | V1 operator loop | Automated acceptance tests pass | **Pass** | `UnitTests/tst_operatoracceptance.cpp` |
| A2 | Preflight corpus | Golden corpus gate in CI | **Pass** | `UnitTestsPreflightCorpus`, `ci.yml` |
| A3 | Bleed fixup | Source PDF unchanged after fixup (save-as) | **Pass** | Operator acceptance SHA-256 test |
| A4 | Attachment paths | Sanitizer + containment on all write paths | **Pass** | `docs/attachment-path-audit.md`, unit tests |
| A5 | Launch actions | Default off; extension prompt | **Pass** | No launch capability exists in this tree: `m_allowLaunchApplications` and every external-launch path are absent (verified 2026-09-12) — owner to confirm the drop was deliberate |
| A6 | URI actions | http/https/mailto allowlist; default off | **Pass** | No URI-action launch path exists in this tree: `pdfprogramcontroller.cpp` is absent and no `mailto`/scheme allowlist is implemented (verified 2026-09-12) — owner to re-baseline the check text |
| A7 | Preflight sidecar | Bounded stdout/stderr; process kill on cancel | **Pass** | Sidecar retired: preflight runs in-process, so no subprocess output surface exists — cancellation is `LoopLibInteraction/sources/preflightcontroller.cpp:154` `cancelRun` (verified 2026-09-12) |
| A8 | PageMaster export | Atomic writes + manifest + cancel | **Pass** | `tst_pagemasterexporttest.cpp` |
| A9 | Manifest/PDF consistency | Roll back output if manifest persist fails | **Pass** (this audit) | `pdfpagemasterexport.cpp` fix |
| A10 | Sentry privacy | No default PII | **Pass, scope corrected** | Desktop sentry-native 0.15.x defaults to no PII; NX-only setter not used. That covers SDK-attached identifiers **only** — crashpad minidumps can still contain PDF content and paths, and no SDK hook can scrub them. The former "no PDF content by design" claim was unenforced by any code; it is now stated as a disclosed property of opting in (R-008) |
| A0 | Preflight report contract | Engine schema version accepted by plugin validator | **Pass** (corrected 2026-07-25) | Both at 3: `preflightengine.h:42`, `preflightsidecarutils.h:38` (`isSupportedSchemaVersion` accepts 1–3). Fix `c515bfa3` merged in `ba428f1b` |
| A11 | CI build | Ubuntu + Windows compile + test | **Pass** | [Run 30779926245](https://github.com/mberrys/Loop-pdf/actions/runs/30779926245) — Ubuntu + Windows `ctest` including `UnitTestsPreflightCorpus` (PR #61) |
| A12 | Installer | Clean-machine install (**Windows + Linux**) | **Pass** (2026-08-03) | Windows: `Invoke-MsiSmokeTest.ps1` wired in `WindowsInstall.yml` — [Run 30792705392](https://github.com/mberrys/Loop-pdf/actions/runs/30792705392) green. Linux: `scripts/smoke-test-appimage.sh` wired in `LinuxInstall.yml` — [Run 30787629154](https://github.com/mberrys/Loop-pdf/actions/runs/30787629154) green. `.deb` built by `ci.yml` but non-functional — not a release artifact |
| A13 | Overprint rendering | Correct overprint compositing in standard page view | **Deferred — mitigation complete** | MIC-320 deferred post-V1: standard page view still renders overprint approximately by default. `overprintDisclosureText()` always shown once a report loads; additional white-overprint warning when finding present (MIC-330). PR #395's canvas fidelity indicator and one-click escalation remain pending until that PR merges into `dev`; see `docs/RENDERER_DIFFERENTIALS.md` and §3 R-002. README + runbook R-002 published |
| A14 | Packaging SBOM / license evidence | MIC-140 checklist complete | **Partial — gates paid distribution, not V1** | Notices generator now resolves versions + license text; artifact SBOM and counsel sign-off outstanding |
| A15 | macOS build | Supported platform | **N/A** | Not a V1 platform; no CI, no package, no notarization |
| A21 | Bundle policy enforcement | No Ghostscript / JRE / Python in default bundle | **Pass** (this revision) | Enforced by `scripts/smoke-test-install.ps1`, wired into `WindowsInstall.yml` |
| A22 | Artifact checksums | SHA-256 published with release | **Pass** (this revision) | `CreateReleaseDraft.yml` emits `SHA256SUMS.txt` |
| A16 | Authentication / payments | Secure flows | **N/A** | Offline desktop; PDF password only |
| A17 | CSP / CORS / cookies | Web security headers | **N/A** | No web app |
| A18 | Browser compatibility | Supported browsers | **N/A** | No embedded browser |
| A19 | OCR product gate | Required for V1 | **N/A — CLI-only** (decided 2026-07-25) | OCR is merged to `master` but V1 ships **CLI-only**: `PdfTool ocr` present; `OcrPlugin.dll` and the bundled sidecar service excluded. `PdfTool ocr` is inert without a user-supplied sidecar. **Enforced (MIC-343):** `OcrPlugin` is gated behind `-DLOOP_PLUGIN_OCR` (default ON for dev builds, explicitly `OFF` in `WindowsInstall.yml`/`LinuxInstall.yml`/the Flatpak manifest); the WIX component that had unconditionally bundled `OcrPlugin.dll` into the MSI is now gated the same way. `LOOP_BUNDLE_OCR_SERVICE` defaults `OFF`. `scripts/smoke-test-install.ps1` fails the scan if `OcrPlugin.dll` is found in an installed tree, closing the drift the plugin/service inclusion previously had no assertion against |
| A20 | Fuzz regression | Weekly fuzz CI | **Pass** (2026-08-04) | `fuzz.yml` fixed (single `permissions:`) and executing. [Run 30803378370](https://github.com/mberrys/Loop-pdf/actions/runs/30803378370) failed with two real JBIG2 findings (see R-003); both fixed in `pdfjbig2decoder.cpp`, and [run 30937285025](https://github.com/mberrys/Loop-pdf/actions/runs/30937285025) is green on `master` at the full 600 s/target budget (MIC-326) |
| A23 | Manifest rollback coverage | Failure path is tested, not just implemented | **Pass** (2026-08-04) | `UnitTests/tst_pagemasterexporttest.cpp` — `manifest_persistFailure_removesNewOutput` and `manifest_persistFailure_keepsOverwrittenOutput` force a real manifest-persist failure (resume run against a manifest in a write-denied directory) and assert both rollback branches. Skipped, not silently passed, where directory permissions are unenforceable (Windows, root) — MIC-335 |
| A24 | Logging / support bundle | Rotating log, scrubbed at write time; reproducible support bundle; no PDF content or minidumps in it | **Pass** (2026-08-09) | `pdf::PDFLogSession` / `pdf::PDFLogScrubber` / `pdf::PDFDiagnosticsCollector` (`LoopLibCore/sources/pdflogger.*`, `pdflogscrubber.*`, `pdfdiagnostics.*`); `PdfTool diagnostics`, Editor Help > Collect Diagnostics; `UnitTests/tst_diagnosticstest.cpp` covers scrubber correctness/idempotence, rotation cap, manifest hash integrity, no-`.pdf`-in-bundle, and the read-only-output-directory failure path (skipped where permissions are unenforceable, same MIC-335 precedent as A23) |

---

OCR is now protected by a release-profile CMake assertion: configuring with
`LOOP_LOOP_DISTRIBUTION=ON` and `LOOP_PLUGIN_OCR=ON` fails explicitly.
The Windows and Linux artifact verifiers also require `PdfTool capabilities
--command ocr` to advertise the CLI command and reject `OcrPlugin` and
`LoopOcrService` artifacts. This turns the CLI-only OCR decision from a
packaging convention into a merge-durable check (issue #41).

## 3. Launch-risk register

Sorted by severity. **Owner** defaults to release engineering unless noted.

### Blocker

| ID | Impact | Affected users | Reproduction | Root cause | Fix / mitigation | Verification | Owner |
|----|--------|----------------|--------------|------------|------------------|--------------|-------|
| **R-001** | Cannot ship Windows installer confidently | All Windows users | Fresh VM without MSVC/Qt; install MSI | Installer pipeline not fully signed off (MIC-301) | Complete MSI smoke test on clean VM. **Do not block on signing** — V1 ships unsigned (MIC-342 / MIC-345) | Install → launch Editor → run preflight on sample PDF | Release |
| **R-002** | Page view does not simulate overprint | Print/prepress shops proofing overprint work | Open an overprint fixture; compare page view against Output Preview | Overprint is implemented in `pdftransparencyrenderer.cpp` (Output Preview) but absent from the standard QPainter path in `pdfpainter.cpp`; that renderer is RGB and overprint is a subtractive CMYK model, so correct handling there is an XL change (MIC-320) | **Accepted for V1:** documented limitation + in-app disclosure. The preflight report panel always tells the operator to use Output Preview once a report is loaded (not only when a `white-overprint` finding is present), with an additional specific warning for the unsafe white/near-white case. PR #395 proposes an independent canvas fidelity banner and one-page escalation to the authoritative `PDFTransparencyRenderer`, but that capability is pending until the PR merges to `dev`; it is not current-state evidence. Escalation is opt-in per page — the default canvas render still approximates overprint, so MIC-320 remains deferred. README documents the general limitation. No "overprint-safe output" claim in marketing | Run preflight on any PDF; confirm the general panel note always appears. Run on `white-overprint-form.pdf`; confirm the additional specific warning also appears; proof via Output Preview. Validate the canvas indicator and toggle only after PR #395 is merged into `dev` | Product |

### High

| ID | Impact | Affected users | Reproduction | Root cause | Fix / mitigation | Verification | Owner |
|----|--------|----------------|--------------|------------|------------------|--------------|-------|
| **R-003** | Malicious PDF crash / DoS | Anyone opening untrusted PDFs | `fuzz_images` on the JBIG2 branch of the harness | **Two real defects, found by the first fuzz run that actually executed** (the workflow had been dead since CodeQL autofix `31a4444d` added a second top-level `permissions:` key; A20 reported "Pass" throughout). (1) Signed-integer overflow at `LoopLibCore/sources/pdfjbig2decoder.cpp:4068` — `PDFJBIG2HuffmanDecoder::readSignedInteger` added a stream-supplied 32-bit range value to a table base value in `int32_t`. (2) Unbounded decode — `SBNUMINSTANCES` (a 32-bit stream value) drives the text-region composition loop, and the halftone grid loop paints one pattern per cell; neither was charged against the existing decode budget, which only accounts for *allocation*. A few input bytes could request billions of composition operations | (1) Arithmetic moved to `int64_t` and range-checked via `checkHuffmanRange`; an out-of-range result is reported as "no value", which callers already handle as out-of-band. (2) The first attempt (`accountCompositionPixels`) charged only composited *pixels*, with a floor of one pixel per item and a 1 GiB budget; the pinned seed `c8a61daa` never reached that code at all, spinning instead in the symbol-dictionary height-class loop (`processSymbolDictionary`), which decodes two arithmetic integers per pass and makes no progress when a height class yields no symbol — invisible to any pixel-based budget because it never touches a bitmap. A second, independent timeout was then found locally while verifying the first fix: `readBitmap` decoding a single legal-size generic-region bitmap took >30 s under ASan/UBSan — bounded in allocation (the existing 512 Mi cap) but not in *time*. Replaced by `accountDecodeWork(items, pixels)`, charging two **independent** budgets: `JBIG2_MAX_TOTAL_DECODE_WORK_ITEMS` (2 Mi items — one per decoded bitmap, symbol instance, halftone grid cell, or symbol-dictionary height class; this is what bounds zero-pixel degenerate loops, regardless of pixel count) and `JBIG2_MAX_TOTAL_DECODE_WORK_PIXELS` (160 Mi decoded/composited pixels — sized from a measured ASan throughput of ~11.2M px/s to keep worst-case decode time to ~15 s, comfortably under the 30 s local verify budget and the 1200 s CI timeout, while still covering pages up to ~600 dpi A3). Splitting pixels from items (rather than one combined "work unit" budget, which was tried and reverted) lets each cap be sized for what it actually bounds — an unavoidable time/size trade-off given a legitimate large scan and a malicious same-size bitmap are indistinguishable until fully decoded. Verified: seed `c8a61daa` now executes in **169 ms** (was >1200 s), the second seed in **46 ms** (was >30 s), the real verify invocation (`-max_total_time=30`) against the regression corpus exits 0 with slowest single input 19 s, and a 15-minute standalone libFuzzer session at `-timeout=20` is clean (13.7M runs, 0 crashes/timeouts) | Fuzz workflow **green** (not merely present); no open critical CVEs — MIC-326 | Security |
| **R-004** | Orphan `PdfTool` if Editor killed hard | Operators canceling preflight | Kill Editor from Task Manager during preflight | OS-level process termination | Document: use in-app cancel; plugin kills child on normal close | Manual checklist item 12 in v1-operator-acceptance | Support |
| **R-005** | Packaging license gaps (Qt LGPL evidence) | Legal/compliance | Audit installer contents | MIC-140 checklist incomplete | Complete `PACKAGING_LICENSING.md` gate before enterprise sales | Checklist sign-off | Legal/Release |
| **R-006** | Flatpak broad filesystem access | Linux Flatpak users | Install Flatpak; inspect permissions | `--filesystem=host` in manifest | Document risk; consider tightening to `home` post-V1 | Flatpak manifest review | Release |

### Medium

| ID | Impact | Affected users | Reproduction | Root cause | Fix / mitigation | Verification | Owner |
|----|--------|----------------|--------------|------------|------------------|--------------|-------|
| **R-007** | Resume batch after manifest failure | PageMaster power users | Disk full during manifest write | Was: PDF written, manifest stale | **Fixed:** remove a PDF this run created when manifest persist fails; keep it (and report the inconsistency) when the write had replaced a pre-existing file, since removing it would destroy user data | **Tested (2026-08-04).** Both branches covered by `manifest_persistFailure_removesNewOutput` and `manifest_persistFailure_keepsOverwrittenOutput` in `UnitTests/tst_pagemasterexporttest.cpp` — MIC-335 | Core |
| **R-008** | Sentry crash minidumps can contain PDF content and file paths | Telemetry users | Crash with Sentry enabled | Crashpad captures thread stacks and referenced heap memory out-of-process. A crash in the parser or content processor therefore has document bytes live in the dump. `before_send` cannot filter this — it applies to events, not the minidump upload | **Disclosure, not enforcement.** Windows builds compile in the Loop DSN. Set `SENTRY_DSN=off` when handling confidential documents. Do not restate "no PDF content by design" — nothing implements it | `PdfTool sentry-verify`; confirm `SENTRY_DSN=off` in confidential workflows | Release |
| **R-009** | Theme/scheme requires restart | All GUI users | Change color scheme in settings | Settings read only at startup | Document in release notes | Manual | UX |
| **R-010** | OCR sidecar supply chain | OCR users | Point `LOOP_OCR_SIDECAR` at unknown binary | External Python/PyInstaller bundle | Ship only signed/bundled sidecar; document env var | OCR README | Release |
| ~~R-011~~ | ~~README links upstream releases~~ | — | — | Fork branding drift | **Resolved.** Every install link in `README.md` points at `mberrys/Loop-pdf/releases`; the only upstream link left is the PDF4QT attribution in the License section, which is correct and stays | README review 2026-08-04 | Docs |

### Low

| ID | Impact | Linear | Notes |
|----|--------|--------|-------|
| **R-012** | Mirror bleed seams on high-contrast art | MIC-339 (Done) | Known V1 limitation (`docs/bleed-stress-test-results.md`, `PRODUCTION_RUNBOOK.md:233`). Operators can switch to pixel-repeat/stretch (MIC-122) |
| **R-013** | Preflight fixups must remain executable end to end | MIC-338 / GitHub #166 | **Resolved in #166.** Editor, PdfTool, and the preflight capability report use the registered `add-bleed`, `downsample-images`, and `rgb-to-cmyk` operations; profile validation rejects unregistered fixup IDs. Each Editor action previews and revalidates a separate output candidate. Editor filtering and `PdfTool capabilities --console-format json` both use the shared registry; regression coverage checks the shipped profile. |
| **R-014** | No macOS CI | MIC-336 | macOS is not a V1 platform; source builds are best-effort (`docs/PLATFORM_SUPPORT.md`). **ID note:** MIC-336 previously reused R-014 to mean "add macOS support, High" — the inverse of this row. R-IDs are now immutable; that override has been removed |
| **R-016** | V1 MSI ships unsigned | **MIC-342** (docs) · **MIC-345** (post-V1) | **Not a launch gate — reaffirmed 2026-08-02.** V1 / 1.0 ships unsigned by design (normal for solo/OSS GitHub Releases). SmartScreen disclosure + `SHA256SUMS.txt` remain (MIC-342). Certificate procurement is commercial/post-V1 only (MIC-345). Earlier “escalated to launch gate” wording in this register was incorrect and is struck |

> **Register hygiene (added 2026-07-25).** Risk IDs are immutable once assigned: a given
> R-number means one thing permanently. Every row above must carry its Linear issue ID, and
> every risk must have one. Both rules were broken — R-014 was reused with an inverted
> meaning, and R-016 was never filed.

---

## 4. Implemented changes (this audit)

### 2026-08-30

| Change | File | Rationale |
|--------|------|-----------|
| Reviewed R-002 and A13 against the gh-49 canvas overprint fidelity indicator (PR #395 on `0.2.0-minor-fixes`, not yet merged to `dev`); strengthened the mitigation description at both entries, the executive table's MIC-330 row, and the R-002 product decision | this file | The indicator is a stronger, automatic (preflight-independent) disclosure layer with a one-click escalation to the authoritative render, but the standard canvas path still approximates overprint by default. MIC-320's deferral and MIC-330's acceptance are explicitly confirmed unchanged — this was a reviewed decision, not a silent skip, since both are release-gate records tied to external Linear tickets |

### 2026-08-04

| Change | File | Rationale |
|--------|------|-----------|
| Cover both manifest-rollback branches with tests | `UnitTests/tst_pagemasterexporttest.cpp` | A23 / R-007 / MIC-335 — the rollback was implemented but no test forced a manifest-persist failure |
| Mark A23 Pass, R-007 tested, R-011 resolved | this file | R-011 was already fixed in `README.md` and had gone stale in the register |
| Flip A20 / R-003 to Pass; restate the executive recommendation | this file | Full-budget fuzz run is green on `master`; no launch gate remains open |

### 2026-08-03 fuzz findings

| Change | File | Rationale |
|--------|------|-----------|
| Range-check Huffman-decoded integers in 64-bit arithmetic | `LoopLibCore/sources/pdfjbig2decoder.cpp` | UBSan signed-integer overflow at `:4068` — R-003 (1) |
| Charge symbol-instance and halftone-grid composition against a new budget | `LoopLibCore/sources/pdfjbig2decoder.cpp` / `.h` | `SBNUMINSTANCES`-driven unbounded decode; libFuzzer timeout >1200 s — R-003 (2) |
| Charge decoded/composited pixels and per-item counts against two independent decode-work budgets | `LoopLibCore/sources/pdfjbig2decoder.cpp` / `.h` | Pixel-only composition budget missed the symbol-dictionary height-class loop entirely (zero-pixel spin); a single combined budget then let a legal-size generic-region decode take >47 s once the pixel cap was loosened for large-scan support — R-003 (2) |
| Add `Fuzz/corpus/regression/` seeds + README for both R-003 (2) timeouts | `Fuzz/corpus/regression/` | Corpus was not committed to any branch; pinning both seeds (including the newly-found symbol-dictionary one) prevents silent regression |
| Mark installer gates Pass; return fuzz gate to blocking | this file §2, §3 | Executive table was stale on all four in-flight rows |

### 2026-08-03 gates 1–4 evidence pass

| Change | File | Rationale |
|--------|------|-----------|
| Add AppImage smoke harness | `scripts/smoke-test-appimage.sh` | MIC-301 Linux half had no install gate |
| Wire AppImage smoke into CI | `.github/workflows/LinuxInstall.yml` | Run smoke after pack; unsigned by default (`SIGN_APPIMAGE` optional) |
| Pin tool checksums | `.github/workflows/LinuxInstall.yml` | `APPIMAGETOOL_SHA256` / `LINUXDEPLOYQT_SHA256` repo vars were unset |
| Fix smoke script exit codes | `scripts/smoke-test-install.ps1`, `scripts/Invoke-MsiSmokeTest.ps1` | PdfTool preflight exit 1 (findings) was failing CI despite passing checks |
| Unsigned + SmartScreen disclosure | `README.md` | MIC-342 |
| Refresh pre-launch checklist | `docs/PRODUCTION_RUNBOOK.md` | Remove stale PR #54 blocker; add workflow evidence rows |
| Owner sign-off package | this file §8 | Step-5 review surface |


| Change | File | Rationale |
|--------|------|-----------|
| Remove duplicate top-level `permissions:` key | `.github/workflows/fuzz.yml` | Workflow was invalid; every fuzz run failed at 0s while A20 reported "Pass" (R-003) |
| Strike R-000; restate executive recommendation | this file | The reported blocker was already fixed on `master` before the revision that reported it |
| Correct A0, A11, A13, A19, A20; add A23 | this file | Several rows asserted verification that did not exist |
| Add Linear IDs and immutability rule to the register | this file | R-014 was reused with an inverted meaning; R-016 had no issue |

### 2026-07-24 changes

| Change | File | Rationale |
|--------|------|-----------|
| Roll back written PDF when batch manifest persist fails | `LoopLibCore/sources/pdfpagemasterexport.cpp` | Prevents resume/state inconsistency (R-007) — tested as of 2026-08-04, see A23 |
| Disable Sentry default PII | `pdfsentry.cpp` / docs | Confirmed desktop 0.15.x has no PII setter (NX-only); default remains off (R-008) |
| Set preflight `QProcess` working directory to app bundle dir | `looppreflightplugin.cpp` (retired with the widget plugin) | Historical: the sidecar no longer ships and the Editor spawns no subprocess for preflight — it runs in-process via `LoopLibInteraction/sources/preflightcontroller.cpp` |

Prior commits on `Pre-P3-sanitize` also addressed bug sanitization and visual polish (see PR #54).

---

## 5. Validation evidence

| Command | Expected | Local VM (2026-07-23) |
|---------|----------|------------------------|
| `cmake --build build --target UnitTestsOperatorAcceptance` | Build OK | **Blocked** — no Qt/vcpkg in cloud VM |
| `ctest -R UnitTestsOperatorAcceptance` | All pass | **Blocked** |
| `ctest -R UnitTestsPreflightCorpus` | All pass | **Blocked** |
| `ctest -R UnitTestsPageMasterExport` | All pass (includes manifest tests) | **Blocked** |
| GitHub `ci.yml` on `master` / PR | Green | **Run in CI** on push |

**Authoritative validation:** GitHub Actions CI on the PR branch.

---

## 6. Not applicable (web SaaS checklist items)

The following were evaluated and are **out of scope** for this desktop product:

- Server-side authentication, sessions, CSRF, CORS, CSP
- Multi-tenant isolation, RBAC, password reset flows
- Payment processing, subscriptions, webhooks
- Browser support matrix (no WebEngine)
- SSRF from server (no server)
- Database migrations / cloud backups
- Rate limiting (no API)
- robots.txt / sitemap / analytics consent banners

Local equivalents are covered above (PDF passwords, attachment sanitization, atomic writes, optional Sentry).

---

## 7. References

- `docs/v1-operator-acceptance.md` — MIC-300 operator loop
- `docs/SPRINT_CYCLE_2_PLAN.md` — MIC-301, MIC-320 gates
- `docs/PACKAGING_LICENSING.md` — MIC-140 bundle policy
- `docs/attachment-path-audit.md` — MIC-303
- `SECURITY.md` — disclosure policy
- `docs/PRODUCTION_RUNBOOK.md` — deploy, rollback, support

---

## 8. Owner review checklist (step 5)

Product owner: review this document and [`docs/PRODUCTION_RUNBOOK.md`](PRODUCTION_RUNBOOK.md) §10 before approving `CreateReleaseDraft.yml`.

- [ ] Confirm the three in-flight workflow runs linked in the executive table are **green** (Windows MSI, Linux AppImage, fuzz)
- [ ] Confirm README Install section matches your SmartScreen guidance (MIC-342)
- [ ] Confirm release notes will state: unsigned installer, no page-view overprint simulation, Windows + Linux only
- [ ] Confirm you accept known limitations in runbook §9 (overprint, unsigned MSI, Flatpak `--filesystem=host`, mirror bleed seams)
- [ ] Sign off here and in Linear: MIC-301, MIC-326, MIC-327, MIC-330, MIC-342

**After sign-off (step 6, out of scope for this pass):** dispatch `CreateReleaseDraft.yml` on the release SHA, publish `v0.1.0-alpha`, attach MSI + AppImage + `SHA256SUMS.txt`.
