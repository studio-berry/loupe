# Loop roadmap extension — 0.5.0 → 0.8.0 (consolidation amendment, 2026-09-06)

> **Status: amended 2026-09-06 — the canonical Notion Roadmap records the consolidation
> amendment of the same date; this document is its accepted scope decomposition.**
> The canonical [Notion Loop Roadmap](https://app.notion.com/p/38f9cb079ddb804a96dbe26b8d86e84f)
> owns milestone sequencing and boundaries; per the change-control section of that page,
> extending the named release train requires an explicit roadmap amendment. This document
> is the scope, design, architecture, and orchestration decomposition for that amendment.
> It creates no execution authority by itself: 0.2.0 remains the current milestone, and
> 0.5.0 cannot activate before 0.4.0 release acceptance.
>
> **Consolidation amendment (2026-09-06).** The originally proposed six releases
> (0.5.0–0.10.0) are consolidated into four: **0.6.0 absorbs the former 0.7.0**
> (production outcome reconciliation) and **0.7.0 absorbs the former 0.9.0** (workflow
> promotion and verified repeat automation); the former 0.10.0 renumbers to **0.8.0**.
> Ceremony sessions (S00 reconcile openers, vertical-integration sessions, qualification
> lanes) were folded into their owning sessions with sub-issue pointers; nothing was
> dropped. Session maps below retain their original numbering as the scope decomposition;
> the executable GitHub issues carry the folded, resequenced numbering noted per chapter.

## 1. Position in the release train

The accepted critical path through 0.4.0 is:

**release correctness → semantic truth → operator interaction → governed correction → bounded automation**

with the release train **0.1.0 → 0.1.1 → 0.2.0 → 0.2.1 → 0.3.0 → 0.4.0**. This extension continues
the same dependency-first logic — each milestone consumes guarantees from the left and may
not recreate or weaken them:

**job context → governed intake → confirmed outcomes → workflow memory → verified repeat automation → platform & 1.0 readiness**

| Version | Canonical name | Platform horizon | Graduated-automation stage |
|---------|----------------|------------------|----------------------------|
| 0.5.0 | Job Spine & Job-Aware Production Context | H3 — job-aware workspace | — (substrate) |
| 0.6.0 | Governed Intake — Scanned Specs, Page-Gated OCR & Request-to-JobSpec | H4 — OCR and intake | — (substrate) |
| 0.7.0 | Production Outcome Reconciliation & Confirmed-Result Ledger | H5 — production learning | 1 — Observe |
| 0.6.0 (cont.) | *(former 0.7.0) Production Outcome Reconciliation & Confirmed-Result Ledger* | H5 — production learning | 1 — Observe |
| 0.7.0 | Workflow Memory, Recommendations & Verified Repeat Automation (consolidates the former 0.8.0 + 0.9.0) | H5 — production learning | 2–5 — Recommend, Draft workflow, Approve, Run with verification |
| 0.8.0 | Platform Hardening, Extension Ecosystem & 1.0 Readiness (formerly titled 0.10.0) | GA preparation | all stages hardened |

"Platform horizon" refers to the dependency-sequenced horizons H3–H5 in
[Loop — Platform Evolution, Workflow Intelligence & Roadmap](https://app.notion.com/p/3b49cb079ddb81cea546c5146054a942);
"graduated-automation stage" refers to that page's five-stage ladder
(Observe → Recommend → Draft workflow → Approve workflow → Run with verification).
Every capability listed under "After 0.4.0 — candidates for the 0.5.x horizon" on the
canonical roadmap is placed exactly once in this train; nothing is silently dropped:

| 0.5.x-horizon candidate (canonical roadmap) | Placed in |
|---------------------------------------------|-----------|
| Thin encrypted job spine (request/job/artwork/output/status) | 0.5.0 |
| Broader request-to-JobSpec intake | 0.6.0 |
| Page-gated OCR and scanned-specification extraction | 0.6.0 |
| Production/RIP/press event import and outcome matching | 0.6.0 (confirmed-result ledger) |
| Workflow recommendations from reconciled repeat jobs | 0.7.0 |
| Approved workflow promotion and repeat automation | 0.7.0 |
| Broader plugin/tool ecosystem | 0.8.0 |
| macOS qualification if prioritized | 0.8.0 (explicit go/no-go decision session) |

### Version-number notes

- SemVer 2.0 continues to govern ([VERSIONING.md](VERSIONING.md)). Each milestone above is
  a feature minor; backward-compatible fixes ride the active minor as patches
  (0.5.1, 0.5.2, …). The consolidation renumbers the tail so the train runs gapless
  0.5.0 → 0.6.0 → 0.7.0 → 0.8.0; the former 0.7.0/0.9.0/0.10.0 planning titles remain
  aliases only.
- While the major version is `0`, minor bumps may still break; 0.8.0's contract-freeze
  work exists precisely to convert that latitude into the 1.0 compatibility promise.
- 1.0.0 is not one of these milestones. 0.8.0 *exits* with the 1.0 release-candidate
  criteria defined, demonstrated on an exact SHA, and a dossier ready for the operator
  to open the 1.0.0-rc train.

### Ordering rationale (why reconciliation precedes recommendation)

The platform page's production-feedback workstream is explicit:
`stable output identity → event import → reviewable matching → confirmed outcome → workflow recommendation`,
and its stated countermeasure for weak learning signal is "learn only from confirmed
outcomes matched to exact outputs." Recommendations therefore cannot ship before the
confirmed-outcome ledger exists. That forces 0.7.0 (reconciliation) before 0.8.0
(recommendations), even though recommendations are the more visible feature. The same
logic gates 0.9.0: automation authority is granted only to workflows whose recommendation
lineage is already evidence-backed and operator-approved.

## 2. Orchestration model for 0.5.0+ — what changes and why

Milestone architecture still defines invariant boundaries, Sessions still define
dependency/exit gates, and linked Issues remain the executable developer specifications.
The [📐 orchestration template](https://app.notion.com/p/3c39cb079ddb81eebf8dd8948612c365)
remains the authoring standard for execution documents. What changes is session sizing
and bookkeeping, based on observed execution friction through 0.2.0–0.4.0 planning.

### Strengths preserved unchanged

1. **Authority order and source-of-truth hierarchy** — code/tests/exact-SHA CI first,
   then ADRs/policies, then roadmap/milestone architecture, then issue acceptance
   criteria, then vision documents.
2. **Exact-SHA evidence discipline** — a merged PR is implementation state; qualification,
   acceptance, promotion, and release are separate gates proven by their own evidence.
3. **Acceptance-state vocabulary** — planned / implementation candidate / merged /
   runtime-proven / externally proven / released-accepted. "Done" is never a state.
4. **Session-zero reconcile-only sessions** and the plan-truth rule (a plan is an
   execution hypothesis, reconciled against the live tree before every session).
5. **Explicit entry/exit gates, stop rules, and out-of-scope lists per milestone.**
6. **Sanctioned-parallelism-only** — lanes run concurrently only where the session map
   marks them; everything else is serial by dependency.
7. **The session handoff packet** (exact SHA, changed files, tests run, deviations,
   remaining risks) as the only valid inter-session interface.

### Weaknesses corrected, with the binding rule that replaces each

1. **Sessions bundled too much scope.** The 0.3.0/0.4.0 execution groupings assign one
   session per A/B/C phase — six to nine issues per session — which exceeds a single
   agent context window and is the direct cause of long, drifting session execution.
   This also contradicts the template's own sizing rule ("prefer 3–8 focused
   implementation tasks per session; one primary ownership boundary per session").
   **Rule: one session = one executable issue = one agent context window.** A session
   owns one primary boundary (contract, Core behavior, persistence, one surface adapter,
   UI, integration, or qualification), carries 3–8 tasks, and completes its slice —
   implementation, tests, evidence, handoff — before ending. Depth over breadth: a
   session never advances two surfaces shallowly instead of finishing one.
2. **Global session ordinals were fragile.** Sessions 00–20 number one cross-milestone
   train, so splitting or inserting a session renumbers everything after it (and stale
   "current session" pointers followed). **Rule: global ordinals end at Session 20
   (0.4.0). From 0.5.0, sessions are milestone-scoped — `0.5.0-S00`, `0.5.0-S01`, … —
   and a split session takes a letter suffix (`S03a`, `S03b`) rather than renumbering
   its successors.**
3. **Issue specifications rotted when authored too early.** Repeated reconciliations
   spent effort marking stale handoffs superseded. **Rule: rolling-wave authoring.**
   This document fixes scope, architecture, invariants, and the session map for all six
   milestones now; the per-session executable issues and the full execution document
   (📐 template form) for a milestone are authored at milestone activation, from the
   milestone's chapter here, after a session-zero reconcile.
4. **Reconciliation diaries accumulated on orchestration pages.** The template already
   directs corrections in place plus one dated entry in the Daily Reconciliation
   database. **Rule: execution documents carry exactly one `Current session` pointer
   field, updated in place; narrative reconciliation history lives only in the
   reconciliation database.**
5. **PR titles and headlines drifted from orchestration truth** (a PR titled "complete
   Sessions 06–13" did not close those gates). **Rule: PR titles, commit messages, and
   document headlines are never acceptance evidence. Only per-session exit evidence on
   an exact SHA closes a gate.**
6. **Early-landed code blurred gate state.** Pre-implemented work (e.g., package/residue
   infrastructure landing while an earlier session was blocked) is welcome but confused
   status. **Rule: early-landed code is reconciled and reused by the owning session;
   its gate still closes only in dependency order, with its own evidence.**

### Session anatomy (normative for 0.5.0+)

Every session in the maps below is specified at activation with: objective (one bounded
result); exact starting SHA; allowed layers/files; components to reuse; components it
must not create (second identity, second scheduler, second truth path…); 3–8 tasks;
named tests; exit evidence; escalation conditions; handoff packet. Session types:

| Type | Owns | May not |
|------|------|---------|
| `reconcile` (S00) | Repository truth, plan classification, scope amendment | Implement features |
| `contract` | One schema/identity/state-machine contract + fixtures | Persistence, UI, surfaces |
| `core` | One bounded Core/behavior change over an accepted contract | New contracts, UI |
| `persistence` | One store/migration boundary, close/reopen/crash proof | Business semantics, UI |
| `surface` | One adapter (CLI, PageMaster) over typed contracts | Semantics divergence |
| `ui` | One Quick workspace slice over typed DTOs | Truth computation, mutation authority |
| `integration` | Connecting independently tested pieces; vertical scenario | Inventing architecture while integrating |
| `qualification` | One hostile/scale/platform lane with budgets | Feature work |
| `audit` | Exit audit, catalogs/docs regeneration, exact-SHA gate, promotion | New behavior |

## 3. Engineering invariants — continuation and additions

Invariants 1–20 of the canonical roadmap continue unchanged and are consumed, not
restated. The 0.5.0+ train adds:

21. **Job context informs; it never silently decides.** Every job field that reaches a
    profile, plan, or recommendation carries per-field source provenance
    (operator / parsed / agent-proposed / imported) and an explicit unknown state.
    Unknown never resolves to a guessed default.
22. **OCR and extraction output is evidence, not truth.** It carries producer, version,
    confidence, and source span; it never overrides live text; it never changes a
    deterministic verdict except by producing cited evidence records that deterministic
    policy evaluates.
23. **Imported production events are untrusted until matched and confirmed.** Only
    operator-confirmed outcomes, matched to exact output digests, may enter the
    confirmed-result ledger or influence any recommendation.
24. **Repeated behavior is evidence, not authorization.** No workflow gains automatic
    execution authority from frequency alone; promotion is an explicit operator ceremony.
25. **Trusted workflows are immutable once approved.** Change produces a new version
    requiring new promotion; active triggers migrate explicitly; nothing self-modifies.
26. **Automation never bypasses the governed path.** A verified auto-run stages output,
    revalidates from declared impact, and produces sign-off records through the same
    0.3.0 gateway as manual work; any divergence, confidence fall, budget breach, or
    non-PASS revalidation pauses to the operator. Silence is never continuation.

## 4. Milestones

---

### 4.1 · 0.5.0 — Job Spine & Job-Aware Production Context

**Consolidation note.** Absorbs the former 0.4.0-B plan-compiler track (GitHub #34, #128) and the 0.2.1 job scheduler (#238). Ceremony sessions S00, S11, and S12 below were folded: reconciliation into S01, the vertical and hostile/scale lane into the S13 exit gate (GitHub #404, #416).

**Horizon H3.** The operator loop gains a durable subject: work stops being "a PDF I
opened" and becomes "a job I'm producing."

**Objective.** Every artifact, preflight run, operation plan, approval, output, and
status event can attach to a durable, local, encrypted job record — the thin spine
linking request → job → artwork → output → status — and job context flows
deterministically into profile resolution and batch execution. Loop becomes the local
system of record for production work without becoming an MIS.

**Value hypothesis (startup lens).** The wedge widens from "tool I run on files" to
"place my production work lives," which is the precondition for every retention and
automation feature after it. Adoption signal: share of preflight/correction work carrying
a job binding; repeat-job open rate on the same spine.

**Entry gate.**
- 0.4.0 released and operator-accepted (agent alpha proven inside authority bounds).
- Product/package identity decision (Loop→Loop) is terminal — required before new
  persisted schema kinds and CLI command families are named.
- Schema-evolution policy (0.1.1) operative: new persisted kinds declare kind +
  major/minor and golden fixtures from day one.
- Profile-variable/job-spec injection substrate from 0.1.1 (#128 lineage) reconciled.

**Architecture.**
- **JobSpec contract** — canonical, versioned schema kind: client, product, finished
  size, quantity, stock, finishing, due date, notes; normalized values with units;
  per-field provenance and unknown state (invariant 21).
- **Job identity & linkage** — `JobId` plus typed link records binding request
  artifacts, artwork artifacts (by `PDFArtifactIdentity`), outputs (by digest), and
  status events; job state machine (draft → active → produced → closed/cancelled, with
  explicit reopen).
- **Encrypted local job store** — a new persistence boundary beside (not inside)
  document provenance; at-rest encryption with an ADR-decided key policy; fail-closed
  on unreadable stores; migrations per schema-evolution policy.
- **Job event log** — append-only, tamper-evident status/provenance events reusing the
  existing provenance-chain discipline; linked to, never duplicating, document
  provenance.
- **Job-aware profile resolution** — deterministic precedence (base → press → stock →
  product → client → job) with per-field provenance; resolved-profile identity stays
  immutable and attributable; unknown job fields produce visible gaps, never defaults.
- **Surfaces** — desktop job panel and job-aware workflows; `PdfTool job` command
  family; PageMaster batch manifests carry job bindings and register outputs by digest.
- **Will not create:** a second artifact identity, a second provenance chain, quoting/
  scheduling/inventory/accounting features, cloud sync, or a Core dependency on the job
  store (Core exposes registration hooks; the spine subscribes).

**Out of scope.** MIS functions (quoting, invoicing, scheduling, inventory), multi-user
collaboration/sync, live e-mail or API intake (0.6.0 owns intake; importers stay
file-based), any recommendation or automation behavior.

**Session map.**

| Session | Type | Delivers | Depends on | Exit evidence |
|---------|------|----------|------------|---------------|
| S00 | reconcile | Base SHA; state of profile-injection substrate, provenance/persistence patterns, encryption options; amended scope | 0.5.0 entry gate | Reconciliation table; issues authored for S01–S13 |
| S01 | contract | JobSpec schema kind v1 + validation + golden fixtures | S00 | Fixture round-trip; unknown-state and provenance semantics tested |
| S02 | contract | Job identity, link records, job state machine, `job-record` schema kind | S01 | State-machine and digest-binding tests green |
| S03 | persistence | Encrypted job store, key-policy ADR, migrations, crash safety | S02 | Create/close/reopen/crash/migration fixtures green; unreadable store fails closed |
| S04 | persistence | Append-only job event log with tamper evidence | S03 | Chain verification + reopen tests green |
| S05 | core | Artifact/output registration hooks binding documents and published outputs to jobs | S02 (parallel to S03/S04 until persistence lands) | Revision-safe binding tests; no Core→store dependency in the target graph |
| S06 | core | Job-aware profile resolution with per-field provenance | S01, S05 | Precedence fixtures incl. unknown-field gaps; resolved-profile identity immutable |
| S07 | surface | `PdfTool job` command family + JSON envelopes + capability discovery | S03–S06 | Envelope schema fixtures; CLI parity tests |
| S08 | surface | PageMaster manifest job binding; outputs registered by digest; resume interplay | S03–S06 | Batch fixtures incl. jobless batches unchanged |
| S09 | ui | Desktop job panel: create/attach/open; spine view; unknown-field visibility | S03–S06 | Quick contract tests over typed DTOs |
| S10 | ui | Job context in preflight/correction flows; approvals and sign-offs carry job identity | S09 | Approval/sign-off records show job binding; stale-context rules tested |
| S11 | integration | Vertical: request → job → artwork → preflight → governed fix → output → status, across desktop/CLI/PageMaster | S07–S10 | Cross-surface parity evidence on one SHA |
| S12 | qualification | Hostile/scale lane: thousands of jobs, corrupted/locked stores, migration and encryption failure modes, budgets | S11 | Budgeted, attributable INCOMPLETE/fail-closed outcomes |
| S13 | audit | Catalog/docs regeneration, exact-SHA release gate, dossier, promotion | S11, S12 | Release Gate green on exact SHA; milestone terminalized |

**Sanctioned parallelism:** S07/S08/S09 may run as concurrent lanes once S06 closes;
S05 may run beside S03/S04. Everything else is serial.

**Exit gate.**
- [ ] JobSpec and job-record schema kinds versioned with golden fixtures; unknown state and per-field provenance proven.
- [ ] Encrypted store reopens across restart, migrates from fixtures, and fails closed when unreadable.
- [ ] Outputs link to jobs by digest; the request→job→artwork→output→status chain is navigable on all surfaces.
- [ ] Job-aware profile resolution is deterministic, attributable, and gap-visible; ambiguity is surfaced, never defaulted.
- [ ] Desktop, CLI, and PageMaster demonstrate semantic parity for job binding.
- [ ] Jobless operation remains fully supported and unchanged.
- [ ] Exact-SHA release qualification evidence exists.

**Stop rules.** Encryption/key management that cannot fail closed is NO-GO. A Core→job-store
dependency, a second artifact identity, or silent default-filling of unknown job fields is
NO-GO. NO-GO means fix or extend the milestone, never ship the weakened spine.

---

### 4.2 · 0.6.0 — Governed Intake & Confirmed Outcomes (absorbs the former 0.7.0)

**Consolidation note.** Executable on GitHub as sessions S01–S17 under milestone
**0.6.0**: the intake arc below (S01–S10, issues #418–#429) runs first, then the former
0.7.0 outcome arc (§4.3, GitHub S11–S17, issues #431–#441).

**Horizon H4 → H5.** Scanned or image-only inputs and unstructured requests become structured,
provenance-carrying JobSpec candidates — always operator-confirmed.

**Objective.** Live-text-first page classification, page-gated OCR as an evidence
producer, deterministic specification-field parsing, and (optionally, per deployment)
agent-assisted extraction through the 0.4.0 inference boundary, assembling candidate
JobSpecs whose every field carries a source span, confidence, and unknown state. The
operator confirms; the 0.5.0 spine records.

**Value hypothesis.** Intake time drops from minutes of retyping to seconds of
confirming; the job spine fills itself from the documents customers already send.
Signals: % of JobSpec fields extracted then confirmed without edit; intake time per job.

**Entry gate.** 0.5.0 released and accepted (candidate specs need a spine to land in).
Locked EasyOCR V1 design ([OCR_EASYOCR_PLAN.md](OCR_EASYOCR_PLAN.md)) and the
`loop-ocr/` sidecar reconciled against current truth, including the V1 "CLI-only, no
bundled sidecar" product decision that this milestone supersedes deliberately.

**Architecture.**
- **OCR evidence contract** — OCR output enters the Evidence Graph as evidence records
  (producer/version, confidence, page/region span, revision-bound); live text always
  wins; OCR failure yields attributable INCOMPLETE, never silent absence (invariant 22).
- **Page gate** — live-text-first classification (live / image-only / mixed), budgets,
  golden corpus; the expensive sidecar starts only when a page needs it.
- **Sidecar lifecycle** — `LoopOcrService` process supervision, exit-code contract,
  offline model provisioning, clean-machine packaging, absent-sidecar fail-closed.
- **Deterministic spec parsers** — dimensions, quantities, stock, dates, color counts,
  with unit/locale normalization; pure functions over OCR/live text; zero model
  dependency; these are the offline fallback.
- **Candidate JobSpec assembly** — parsed fields merge into a candidate spec with spans,
  confidence, conflicts, and unknowns; diffed against any existing job record.
- **Agent-assisted extraction (optional capability)** — schema-constrained field
  proposals through the existing 0.4.0 provider boundary, citing OCR spans; the
  prompt-injection corpus extends to hostile scanned content; offline deployments run
  parsers only.
- **Surfaces** — `PdfTool intake`/`ocr` envelopes; desktop intake-review workspace with
  per-field confirm/edit/reject and span navigation to page regions.
- **Will not create:** searchable-PDF write-back, OCR-driven verdict changes, live
  inbox/API integrations, training pipelines, handwriting recognition.

**Out of scope.** Everything in "will not create," plus batch OCR in PageMaster and any
learning behavior.

**Session map.**

| Session | Type | Delivers | Depends on | Exit evidence |
|---------|------|----------|------------|---------------|
| S00 | reconcile | State of `loop-ocr/`, sidecar packaging, MIC-343 CLI-only decision, 0.4.0 boundary reuse points | 0.6.0 entry gate | Reconciliation table; issues authored |
| S01 | contract | OCR-evidence schema kind; live-text precedence; confidence semantics | S00 | Fixture round-trip; precedence tests |
| S02 | core | Page-gate hardening: classification classes, budgets, corpus | S01 | Golden classification corpus green; INCOMPLETE attribution tested |
| S03 | persistence | Sidecar lifecycle + packaging qualification (models, exit codes, clean machine) | S00 | Clean-machine smoke; absent-sidecar fail-closed test |
| S04 | core | OCR results into the Evidence Graph; revision fencing; stale discard | S01, S02 | Evidence records citeable; stale-result tests green |
| S05 | core | Deterministic spec-field parsers + unit/locale normalization | S01 | Parser fixture suite green; zero model dependency proven |
| S06 | contract | Candidate-JobSpec assembly: spans, confidence, conflicts, unknowns, job diff | S05 | Assembly fixtures incl. conflict and unknown cases |
| S07 | core | Agent-assisted extraction proposals via 0.4.0 boundary; injection corpus extended to OCR text | S06 | Schema-validated proposals; injection corpus green; offline fallback proven |
| S08 | surface | `PdfTool intake` + productized `ocr` envelopes | S04–S06 | Envelope fixtures; CLI/desktop parity contract |
| S09 | ui | Intake review workspace: per-field confirm/edit/reject; span navigation | S06 (S07 optional) | Quick contract tests; confirmed merge lands in job spine |
| S10 | integration | Vertical: scanned request → gate → OCR → parse → candidate → confirm → job record | S08, S09 | End-to-end scenario on one SHA with full provenance |
| S11 | qualification | Adversarial lane: garbage scans, injection corpus, huge pages, sidecar absence, retry determinism | S10 | Budgeted fail-closed evidence |
| S12 | audit | Exit audit, catalogs/docs, exact-SHA gate, promotion | S10, S11 | Release Gate green; milestone terminalized |

**Sanctioned parallelism:** S03 beside S01/S02; S05 beside S03/S04; S08/S09 as lanes
after S06.

**Exit gate.**
- [ ] OCR never replaces live text and never changes a deterministic verdict without cited evidence records.
- [ ] Every candidate JobSpec field shows source span, confidence, provenance, and unknown state; operator confirmation is the only path into the spine.
- [ ] Deterministic parsers provide full offline capability; agent assistance degrades to them cleanly.
- [ ] Injection corpus (including hostile scanned content) cannot expand authority or alter policy.
- [ ] Sidecar-absent and OCR-failure paths are attributable INCOMPLETE, exercised on both platforms and clean machines.
- [ ] Exact-SHA release qualification evidence exists.

**Stop rules.** Any path where OCR text silently becomes document truth, verdict input,
or unconfirmed job data is NO-GO. Packaging that requires network access for core intake
is NO-GO.

---

### 4.3 · (former 0.7.0, absorbed into 0.6.0) — Production Outcome Reconciliation & Confirmed-Result Ledger

**Consolidation note.** Retained verbatim as the scope decomposition for 0.6.0 sessions
S11–S17 (GitHub issues #431–#441, milestone 0.6.0).

**Horizon H5, automation stage 1 — Observe.** Loop learns what actually happened on
press before it is allowed to suggest anything.

**Objective.** Import production/RIP/press events through a stable file-based importer
boundary, normalize them, match them to exact jobs and outputs (by digest, identifiers,
and fingerprints) with deterministic scoring, route ambiguity to an operator review
queue, and append confirmed outcomes — including grouped samples, retries, splits, and
partials — to the job spine. Observe-mode only: record and display, recommend nothing.

**Value hypothesis.** The job record becomes trustworthy history — "what we actually ran
and how it ended" — which operators value directly (job lookup, reprint context) and
which is the sole legal training signal for the recommendation arc (0.7.0). Signals: % of outputs with confirmed
outcomes; median time-to-reconcile; review-queue precision.

**Entry gate.** Mid-milestone arc gate: the 0.5.0 spine is in production use and the
0.6.0 intake arc (S01–S10) is accepted before the outcomes arc (S11–S17) activates. At
least one real
production event source (even a manually exported log) identified per pilot deployment —
importers are built against measured availability, not imagined APIs.

**Architecture.**
- **Production-event contract** — versioned schema kind: event kinds (queued, started,
  produced, failed, reprint, sample, cancelled), device/source identity, timestamps,
  raw-payload provenance; classified untrusted at import (invariant 23).
- **Importer boundary** — file-based importers first (CSV/JSON, one XML/JDF-shaped
  mapping); import batches carry source identity and digest; validation fails closed;
  re-import is idempotent. No live API is an architecture assumption.
- **Matching engine** — deterministic candidate generation (output digest, job-id echo,
  filename fingerprint, time window) with explicit scoring and ambiguity thresholds;
  pure and fixture-tested.
- **Review queue + confirmation** — operator confirms/rejects matches; confirmations
  append outcome events to the job event log; nothing unconfirmed enters the ledger.
- **Run grouping** — samples, retries, split runs, and partials group into production
  runs with their own state machine (the platform page's run-reconciliation cases are
  the fixture set).
- **Observe-mode analytics** — chain-completeness and job-history views; explicitly no
  recommendations.
- **Will not create:** press-vendor API clients as dependencies, automatic confirmation,
  any influence from unconfirmed events, scheduling/costing analytics.

**Out of scope.** Recommendations and automation (0.7.0), bidirectional press
control, cost/wage analytics.

**Session map.**

| Session | Type | Delivers | Depends on | Exit evidence |
|---------|------|----------|------------|---------------|
| S00 | reconcile | Base SHA; available real event-source formats per pilot; amended scope | 0.7.0 entry gate | Reconciliation table; issues authored |
| S01 | contract | Production-event schema kind + untrusted classification | S00 | Fixture round-trip; validation fail-closed |
| S02 | surface | File importers (CSV/JSON + one XML/JDF-shaped mapping); idempotent batches | S01 | Importer fixtures incl. malformed feeds |
| S03 | core | Matching model: candidates, scoring, ambiguity thresholds (pure) | S01 | Deterministic matching fixture suite |
| S04 | persistence | Review queue + confirmation persistence; confirmed outcomes append to spine | S03 | Append-only proof; idempotent re-import; reopen tests |
| S05 | core | Run grouping semantics (samples/retries/splits/partials) | S04 | Grouping fixtures from platform-page cases |
| S06 | surface | `PdfTool outcomes` family: import/match/review-list/confirm with actor identity | S04, S05 | Envelope fixtures; CLI parity |
| S07 | ui | Review workspace: match queue, score display, confirm/reject, chain view | S04, S05 | Quick contract tests; confirmed chain renders |
| S08 | ui | Observe-mode job-history and chain-completeness views | S07 | Read-only proof: no suggestion affordance exists |
| S09 | integration | Vertical: import → match → review → confirm → complete chain, cross-surface | S06–S08 | Scenario evidence on one SHA |
| S10 | qualification | Hostile lane: malformed/duplicate/conflicting events, clock skew, spoofed identifiers, huge histories | S09 | Fail-closed + budget evidence |
| S11 | audit | Exit audit, catalogs/docs, exact-SHA gate, promotion | S09, S10 | Release Gate green; milestone terminalized |

**Sanctioned parallelism:** S02 beside S03; S06/S07 as lanes after S05.

**Exit gate.**
- [ ] Only operator-confirmed, digest-matched outcomes exist in the ledger; unconfirmed events are visibly pending or rejected.
- [ ] Ambiguous matches always route to review; no auto-confirmation path exists.
- [ ] Samples, retries, splits, and partials group correctly on the fixture corpus.
- [ ] Re-import is idempotent; malformed feeds fail closed with attributable errors.
- [ ] The full chain request → JobSpec → artwork → decisions → workflow → confirmed result is navigable.
- [ ] Exact-SHA release qualification evidence exists.

**Stop rules.** Any path where a written file or queued job counts as a confirmed
production outcome is NO-GO. Learning-adjacent behavior (suggestions, rankings) shipping
from this milestone is NO-GO.

---

### 4.4 · 0.7.0 (part 1) — Workflow Memory & Evidence-Backed Recommendations

**Consolidation note.** Formerly milestone 0.8.0; executable on GitHub as 0.7.0 sessions
S01–S08 (issues #443–#453).

**Horizon H5, automation stages 2–3 — Recommend & Draft workflow.** After one verified
success, suggest; after repeated consistent successes, assemble a draft for review.

**Objective.** Recognize repeat jobs from confirmed reconciled chains using deterministic
similarity signals (client, product, finished size, quantity range, stock/finishing,
artwork fingerprint, geometry/page count, prior operations), recommend the previously
approved workflow with visible assumptions and differences, record every recommendation
decision, and — for consistently repeated work — compile a draft reusable workflow
(Action List + profile + required inputs) for operator review. Nothing executes from a
recommendation; accepted recommendations pre-fill the normal 0.3.0 governed path.

**Value hypothesis.** This is the retention engine: setup time on repeat jobs collapses,
and the accept/edit/reject stream is a measurable quality signal. Signals:
recommendation accept + accept-with-edit rate; repeat-job setup time; drafts promoted
later in the automation arc (0.7.0 part 2).

**Entry gate.** The 0.6.0 confirmed-outcome ledger accepted **and** a minimum confirmed-outcome corpus exists (target
per the platform page's experiment design: a reconciled dataset on the order of 200–500
completed jobs, or the pilot-scaled equivalent recorded in the activation reconcile). If
the corpus is too thin, the milestone pauses rather than lowering the evidence bar.

**Architecture.**
- **Repeat-signal contract** — versioned feature schema over confirmed chains;
  normalization rules; no free-text similarity.
- **Artwork fingerprint** — deterministic structural/perceptual digest, bounded compute,
  collision-characterized; an evidence input, never an identity substitute.
- **Matching + scoring policy** — candidate retrieval, score composition, thresholds,
  and hard stop conditions (never recommend across material mismatches, e.g. differing
  finished size or stock, regardless of score).
- **Recommendation records** — versioned schema kind citing the chains (evidence IDs)
  that justify each recommendation; shown/accepted/edited/rejected events append-only
  (invariant 24 groundwork).
- **Draft-workflow assembly** — compiles prior approved operation sequences + resolved
  profile into an existing Action List draft with an assumptions list and required-input
  manifest; compile-only, executed — if at all — through the normal manual path.
- **Evaluation harness** — the platform page's three capabilities measured separately
  (request parsing, workflow recommendation, run reconciliation) on a named corpus.
- **Optional agent explanation** — the 0.4.0 boundary may explain a recommendation; the
  matching itself is deterministic.
- **Will not create:** opaque ML ranking, cross-customer data pooling, auto-applied
  recommendations, self-updating drafts.

**Out of scope.** Promotion and any automatic execution (0.7.0 part 2); pricing/scheduling
suggestions.

**Session map.**

| Session | Type | Delivers | Depends on | Exit evidence |
|---------|------|----------|------------|---------------|
| S00 | reconcile | Base SHA; confirmed-corpus size vs. entry bar; amended scope | 0.8.0 entry gate | Reconciliation table; corpus decision recorded; issues authored |
| S01 | contract | Repeat-signal feature schema + normalization | S00 | Feature fixtures green |
| S02 | core | Artwork fingerprint: design, determinism, collision characterization | S01 | Fingerprint fixture corpus; bounded-compute proof |
| S03 | core | Matching + scoring + thresholds + material-mismatch stop conditions | S01, S02 | Deterministic scoring suite; stop-condition tests |
| S04 | contract | Recommendation record schema; shown/decision provenance events | S03 | Append-only decision-event tests |
| S05 | core | Draft-workflow assembly into Action List drafts (compile-only) | S03 | Draft compiles into existing registry formats; no execution path |
| S06 | surface | `PdfTool jobs similar` / `jobs recommend` (read-only envelopes) | S03, S04 | Envelope fixtures; CLI parity |
| S07 | ui | Recommendation UX: repeat banner, assumptions, differences-from-last-time, accept/edit/reject with reasons | S04, S05 | Quick contract tests; accept pre-fills governed plan only |
| S08 | qualification | Evaluation harness on the named reconciled corpus; metric report with corpus identity | S03–S05 | Metrics recorded per capability; no universal claims |
| S09 | integration | Vertical: repeat job → recommendation → accept → plan compiled → normal approval/execution | S06, S07 | Scenario evidence on one SHA |
| S10 | qualification | Adversarial/drift lane: near-miss jobs, adversarial similarity, wrong-recommendation cost review | S09 | Threshold evidence; documented failure modes |
| S11 | audit | Exit audit, catalogs/docs, exact-SHA gate, promotion | S09, S10 | Release Gate green; milestone terminalized |

**Sanctioned parallelism:** S02 beside S04 prep; S06/S07 as lanes after S05; S08 beside
S06/S07.

**Exit gate.**
- [ ] Recommendations appear only for confirmed-outcome-backed matches and always cite their chains.
- [ ] Assumptions and differences are visible on every recommendation; material mismatches hard-stop regardless of score.
- [ ] Every recommendation decision (shown/accepted/edited/rejected) is recorded append-only.
- [ ] Draft workflows compile into existing Action List/registry formats and cannot execute from draft state.
- [ ] Evaluation metrics exist for a named corpus; accept/edit/reject rates are measurable in-product.
- [ ] Exact-SHA release qualification evidence exists.

**Stop rules.** Recommendation quality below the usefulness bar on the evaluation corpus
blocks promotion of the milestone — fix signals or thresholds; never lower the evidence
bar or hide assumptions. Any auto-execution affordance is NO-GO.

---

### 4.5 · 0.7.0 (part 2) — Workflow Promotion & Verified Repeat Automation

**Consolidation note.** Formerly milestone 0.9.0; executable on GitHub as 0.7.0 sessions
S09–S17 (issues #455–#466).

**Horizon H5, automation stages 4–5 — Approve & Run with verification.** The largest
authority expansion in Loop's history, and therefore the most heavily gated.

**Objective.** An operator can promote a draft workflow into an approved, versioned,
immutable trusted workflow with explicit matching rules, required inputs, safety checks,
safe-action tiers, and stop conditions. On a matching job, Loop proposes — or, where the
promotion policy pre-authorizes it, executes — the approved workflow through the exact
0.3.0 governed path, staging output, revalidating from declared impact, and producing
sign-off records; it pauses to the operator whenever confidence falls, material inputs
differ, budgets are exceeded, or revalidation is not PASS. Trusted workflows never
modify themselves (invariants 24–26).

**Value hypothesis.** Repeat work approaches zero-touch while every run stays auditable —
the economic payoff of the whole trust architecture, and the moat: competitors can bolt
on automation, but not automation with this provenance. Signals: % of matching repeat
jobs run under approved workflows; pause precision (pauses that operators judge
warranted); zero unsafe auto-runs — a hard metric, not a hope.

**Entry gate.** The 0.7.0 recommendation arc (part 1) accepted with recommendation quality proven on the evaluation
corpus; promotion-policy decisions locked by ADR before implementation (trigger rules,
stop conditions, confidence policy, safe-action tiers — the "decisions to lock" list
from the platform vision).

**Architecture.**
- **Trusted-workflow contract** — versioned, immutable once approved; matching rules;
  required inputs; safety checks; per-step safe-action tier (propose-only / one-click /
  pre-authorized); stop conditions and confidence policy.
- **Promotion ceremony** — explicit operator review and approval binding actor, workflow
  digest, and matching-rule digest into provenance; revocation/retire path from day one.
- **Match-trigger evaluation** — deterministic trigger checks on new jobs with
  material-input diff computation; evaluation is inert (no execution side effects).
- **Pre-authorized execution policy** — bounded approval delegation expressed inside the
  existing 0.3.0 plan/approval records (an approval that names the workflow version and
  bounds, granted at promotion time); stale-plan and stale-revision rules apply
  unchanged; the agent cannot promote, and promotion cannot be proposed into existence.
- **Divergence + pause semantics** — a first-class paused state with an operator queue;
  resume requires explicit action; every pause and resume is provenance.
- **Staged output + auto-sign-off binding** — automated runs stage, revalidate, and
  bind sign-off records carrying workflow identity/version, trigger evidence, and pause
  history.
- **Lifecycle governance** — supersede/new-version/retire ceremonies; active-trigger
  migration; no in-place edits.
- **Will not create:** self-modifying workflows, frequency-based auto-promotion, a
  bypass around preview/approval/revalidation, unattended fleet orchestration.

**Out of scope.** Cross-site/multi-tenant automation, unattended operation without a
reachable operator, agent-initiated promotion.

**Session map.**

| Session | Type | Delivers | Depends on | Exit evidence |
|---------|------|----------|------------|---------------|
| S00 | reconcile | Base SHA; promotion-policy ADR state; amended scope | 0.9.0 entry gate | Reconciliation table; issues authored |
| S01 | contract | Trusted-workflow schema: matching rules, tiers, stop conditions, immutability | S00 | Fixture round-trip; immutability tests |
| S02 | core | Promotion ceremony + revocation; provenance binding | S01 | Promotion/retire events append-only; actor binding tested |
| S03 | core | Match-trigger evaluation + material-input diff (inert) | S01 | Deterministic trigger fixtures; zero side effects proven |
| S04 | core | Pre-authorized execution policy inside the governed gateway | S02, S03 | Delegated-approval records validate; stale rules enforced |
| S05 | core | Divergence/pause semantics: paused state, operator queue, resume ceremony | S04 | Every divergence class pauses in tests; silent-continue impossible |
| S06 | core | Staged outputs + mandatory revalidation + auto-sign-off record binding | S04 | Sign-off records carry workflow/trigger/pause identity |
| S07 | surface | CLI + PageMaster parity for approved-workflow runs | S04–S06 | Cross-surface semantic parity fixtures |
| S08 | ui | Supervision UX: run timeline, pause queue, approve-to-continue, audit view | S05, S06 | Quick contract tests; pause queue drives resume |
| S09 | core | Lifecycle governance: supersede, new-version, retire, trigger migration | S02 | Lifecycle fixtures; in-place edit impossible |
| S10 | integration | Vertical: matching repeat job end-to-end with injected divergence → pause → resolve → complete | S07–S09 | Scenario evidence on one SHA |
| S11 | qualification | Adversarial lane: spoofed matches, drifted artwork, hostile job fields, concurrent runs, crash/recovery mid-run | S10 | Fail-closed + recovery evidence |
| S12 | audit | Exit audit, catalogs/docs, exact-SHA gate, promotion | S10, S11 | Release Gate green; milestone terminalized |

**Sanctioned parallelism:** S07/S08 as lanes after S06; S09 beside S07/S08.

**Exit gate.**
- [ ] Only operator-promoted workflow versions can pre-authorize execution; promotion, revocation, and supersession are provenance events.
- [ ] Every automated run stages output, revalidates from declared impact, and binds a sign-off record through the 0.3.0 gateway.
- [ ] Every divergence class (material input, confidence, budget, non-PASS) pauses; resume is an explicit operator ceremony.
- [ ] Trusted workflows are immutable; change requires a new version and new promotion; active triggers migrate explicitly.
- [ ] Desktop, CLI, and PageMaster runs of the same workflow version are semantically identical.
- [ ] The adversarial lane (spoofing, drift, injection, concurrency, crash-recovery) passes fail-closed.
- [ ] Exact-SHA release qualification evidence exists.

**Stop rules.** Any silent continuation past a divergence, any execution outside the
governed gateway, or any self-modification of a trusted workflow is NO-GO and halts the
milestone — automation ships late or not at all before it ships unverified.

---

### 4.6 · 0.8.0 (formerly 0.10.0) — Platform Hardening, Extension Ecosystem & 1.0 Readiness

**Consolidation note.** Formerly milestone 0.10.0; executable on GitHub as 0.8.0 sessions S01–S12 (issues #468–#479). Absorbs the 0.4.0-A operator-help content (#53, #159 into S10), the SBOM track (#263 into S07), distribution deferrals (#39, #40 into S07; #43 is the S08 go/no-go), and the 0.3.0 application-quality carryover register (#49, #144, #146, #155).

**GA preparation.** Freeze what 1.0 will promise, open what the ecosystem needs, and
prove the platform at production scale.

**Objective.** Declare and freeze the 1.0 public contract surface (CLI envelopes,
persisted schema kinds, plugin capability ABI, documented behavior) with a deprecation
policy; ship the versioned plugin SDK with admission policy and threat review; run the
performance/scale program on named benchmark identities; harden distribution (signing
procurement decision, update channel, SmartScreen exit); decide macOS explicitly; refresh
the security/privacy audit; complete operator documentation; and assemble the 1.0
readiness dossier with release-candidate criteria demonstrated on an exact SHA.

**Value hypothesis.** Commercial credibility: installable without warnings, upgradeable
in place, extendable by third parties, documented for self-serve onboarding — the
difference between an impressive tool and a sellable platform. Signals: onboarding
completion rate, crash-free session rate, update adoption, first external plugins.

**Entry gate.** 0.7.0 accepted. Open V1-era deferrals re-decided rather than inherited:
installer signing (deferred post-V1 to the paid-distribution decision), macOS (deferred
post-V1), overprint-simulation limitation disclosure posture.

**Architecture.**
- **Contract freeze** — enumerate the public API per [VERSIONING.md](VERSIONING.md);
  everything is either frozen-for-1.0 with fixtures or explicitly experimental;
  deprecation policy with timelines.
- **Schema matrix closure** — every persisted schema kind carries current + previous
  golden fixtures; unsupported majors fail closed, proven kind by kind.
- **Plugin SDK** — the 0.1.1/0.2.0 capability/ABI boundaries become documented,
  versioned, exampled contracts; admission policy; no plugin truth paths.
- **Performance/scale program** — benchmark identities (commit, compiler, OS, Qt,
  hardware, fixture digest) across startup/open/render/preflight/batch/job-store;
  budgets enforced as regression gates; years-of-jobs scale fixtures.
- **Distribution** — signing procurement executed or explicitly re-deferred by decision;
  update channel policy; checksums continuity.
- **macOS** — a go/no-go decision session producing an ADR either way; drift is not a
  decision.
- **Security/privacy** — threat-model refresh (shell, sidecars, plugins, encrypted
  stores, provider outbound-data policy), fuzz budget refresh.
- **Will not create:** new product-scope features hiding as "hardening," a web shell, a
  second language runtime.

**Out of scope.** New capability families; anything that widens authority boundaries.

**Session map.**

| Session | Type | Delivers | Depends on | Exit evidence |
|---------|------|----------|------------|---------------|
| S00 | reconcile | Base SHA; public-contract inventory; deferred-decision register | 0.10.0 entry gate | Reconciliation table; issues authored |
| S01 | contract | 1.0 contract-surface declaration + deprecation policy | S00 | Frozen/experimental classification complete; policy ADR accepted |
| S02 | qualification | Schema compatibility matrix closure (all kinds, current + previous, fail-closed majors) | S01 | Golden-fixture matrix green |
| S03 | contract | Plugin SDK: capability contract docs + example plugin | S01 | Example plugin builds against documented contract only |
| S04 | qualification | Plugin admission policy + threat review + abuse corpus | S03 | Capability-enforcement and abuse tests green |
| S05 | qualification | Performance program: benchmark identities + enforced budgets | S00 | Baseline records with full identity; regression gate wired |
| S06 | qualification | Scale lane: years-of-jobs/ledger fixtures, long-session soak, memory budgets | S05 | Bounded behavior evidence at scale |
| S07 | surface | Distribution hardening: signing decision executed, update channel, SmartScreen exit or documented re-deferral | S00 | Signed-or-decided evidence; update policy tested |
| S08 | audit | macOS go/no-go ADR (+ minimal qualification matrix if go) | S00 | Accepted ADR; matrix evidence if go |
| S09 | qualification | Security/privacy audit refresh: threat model, encrypted stores, provider data policy, fuzz budgets | S03, S07 | Audit report; findings triaged to fixes or accepted risks |
| S10 | ui | Operator documentation + first-run onboarding + sample corpus | S01 | Docs shipped in-product; onboarding scenario tested |
| S11 | audit | 1.0 readiness dossier: rc criteria, known-limitations register, upgrade guide | S01–S10 | Dossier complete; rc criteria demonstrated on exact SHA |
| S12 | audit | Exit audit + promotion; open the 1.0.0-rc train | S11 | Release Gate green; operator acceptance |

**Sanctioned parallelism:** S02/S03 after S01; S05/S07/S08 as independent lanes; S09
after S03+S07.

**Exit gate.**
- [ ] The 1.0 public contract surface is declared, fixture-frozen, and deprecation-governed; experimental surfaces are labeled.
- [ ] An example third-party plugin builds and runs against only the documented SDK.
- [ ] Benchmarks with full identity run as regression gates within budgets, including job-store scale.
- [ ] Distribution decisions (signing, updates, macOS) are executed or explicitly decided — none inherited by drift.
- [ ] Security/privacy audit findings are fixed or accepted on the record.
- [ ] The 1.0 readiness dossier exists and its rc criteria are demonstrated on an exact SHA.

**Stop rules.** A contract that cannot be fixture-frozen is redesigned or labeled
experimental — never silently promised. Scale/performance failures block rc; missing
platform evidence is unavailable, not PASS.

---

## 5. Cross-milestone ownership — additions

One owner per concern, extending the canonical table. Later milestones may build on an
owner's contracts; they may not fork or weaken them.

| Concern | Owning milestone | Later milestones may | Later milestones may not |
|---------|------------------|----------------------|--------------------------|
| Job identity, spine, JobSpec, job-aware profiles | 0.5.0 | Attach evidence, outcomes, recommendations, automation to the spine | Fork a second job model or write unproven data into it |
| Intake evidence (OCR, spec extraction, candidate specs) | 0.6.0 | Consume confirmed fields and cited evidence | Let extraction output become truth without confirmation |
| Outcome ledger, event import, matching, run grouping | 0.7.0 | Cite confirmed outcomes as learning signal | Learn from unconfirmed events or auto-confirm matches |
| Repeat signals, recommendations, draft workflows | 0.8.0 | Promote drafts through 0.9.0 ceremonies; explain via agent | Auto-execute or self-update recommendations |
| Promotion, trusted workflows, verified automation | 0.9.0 | Extend supervised running to new operation families | Bypass staging/revalidation/sign-off or mutate trusted workflows |
| Public contracts, plugin SDK, performance budgets, distribution | 0.10.0 | Version additions under SemVer and deprecation policy | Break frozen 1.0 contracts without a major |

## 6. Risks and countermeasures (new territory)

| Risk | Failure mode | Countermeasure |
|------|--------------|----------------|
| MIS scope creep | Job spine grows quoting/scheduling/accounting until the roadmap is a mediocre ERP | Invariant "thin spine"; explicit will-not-create lists; new product categories require a separate architectural decision |
| Weak learning signal | Files written or queued mistaken for produced outcomes | 0.7.0 confirmed-outcome ledger is the only learning input (invariant 23) |
| Automation trust collapse | One unsafe auto-run destroys operator trust permanently | 0.9.0 pause-on-everything semantics, staged outputs, mandatory revalidation, zero-unsafe-runs as a tracked hard metric |
| OCR overreach | Extracted text quietly becomes document or job truth | Invariant 22; operator confirmation as the only spine entry; injection corpus over scanned content |
| Session sprawl | Many small sessions lose the architectural thread | Contracts-first ordering, integration-only sessions, handoff packets, and this document as the fixed frame |
| Corpus starvation | 0.8.0 learning quality unprovable on thin pilot data | Entry bar + pause rule instead of lowered evidence standards |
| Premature integrations | Press-vendor APIs become architecture assumptions | File-based importer boundary; measured availability before dependency |
| Contract freeze too early/late | 1.0 promises churn, or experimental surfaces calcify | 0.10.0 explicit frozen/experimental classification with fixtures and deprecation policy |

## 7. Milestone activation procedure (rolling wave)

At each milestone's activation (its predecessor's release acceptance):

1. Author the milestone's execution document from its chapter here, in
   [📐 template](https://app.notion.com/p/3c39cb079ddb81eebf8dd8948612c365) form, with a
   child architecture contract if the milestone introduces new binding structure.
2. Run `S00` (reconcile-only): resolve the exact base SHA, classify every planned
   session against repository truth, amend the session map (split with letter suffixes,
   remove satisfied work), and record the reconciliation once in the Daily
   Reconciliation database.
3. Author one executable GitHub issue per session (S00's output), each carrying the
   session's objective, boundary, tasks, tests, exit evidence, and escalation
   conditions. Issues for later milestones are not authored in advance.
4. Set the execution document's single `Current session` pointer; update it in place as
   gates close.
5. Sync GitHub milestone descriptions from `docs/github-milestones/` via
   `scripts/github/sync_milestones.py`.

## 8. References

- Canonical roadmap (owns sequencing): https://app.notion.com/p/38f9cb079ddb804a96dbe26b8d86e84f
- Consolidation amendment (2026-09-06): Notion Roadmap "After 0.4.0 — the amended planned
  train (0.5.0 → 0.8.0)" section; retired GitHub milestone titles 0.9.0 and 0.10.0.
- Orchestration hub: https://app.notion.com/p/3c09cb079ddb80cb9a31ee5dd083739d
- 📐 Orchestration template: https://app.notion.com/p/3c39cb079ddb81eebf8dd8948612c365
- Platform evolution & workflow intelligence vision: https://app.notion.com/p/3b49cb079ddb81cea546c5146054a942
- Repository: [VERSIONING.md](VERSIONING.md) · [SCHEMA_EVOLUTION.md](SCHEMA_EVOLUTION.md) ·
  [OCR_EASYOCR_PLAN.md](OCR_EASYOCR_PLAN.md) · [LOOP_WORKSPACES.md](LOOP_WORKSPACES.md) ·
  [V1_RELEASE_READINESS.md](V1_RELEASE_READINESS.md) · [github-milestones/](github-milestones/README.md)
