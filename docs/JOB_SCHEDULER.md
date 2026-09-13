# Loop job scheduler

`PDFJobScheduler` is the application-wide Core submission boundary for work
that can outlive the initiating interaction. Applications should use
`PDFJobScheduler::global()` (or an explicitly owned scheduler in tests) rather
than starting an independent long-running `QtConcurrent`, `QThreadPool`, or
`std::thread` operation.

## Job contract

Every `PDFJobSpec` carries:

- `jobId`, artifact identity, and document key/revision;
- the operation or check identifier;
- one of the six ordered priority classes;
- a `PDFJobKind` identifying rendering, preflight, OCR, export, thumbnails,
  batch, or agent work;
- a `PDFProcessingLimits` budget and progress model; and
- an explicit stale-result policy.

The work callback receives a `PDFJobContext`. It exposes the shared
`PDFOperationControl` cancellation view, a processing budget, progress
reporting, a result summary, and an optional output artifact. A caller may
present output only after the scheduler reports `Succeeded`. `Cancelled`,
`Failed`, and `Stale` are terminal non-success states and must not be treated
as completed output.

## Priority and cancellation

The scheduler uses a fixed worker count and a stable priority queue. Lower
numeric priority values run first; jobs at the same priority retain submission
order. With more than one worker, one worker slot is reserved for interaction,
visible-page, near-viewport, and operator jobs so a saturating background queue
cannot consume all capacity needed to produce visible work. Cancellation is
cooperative because PDF processing primitives already
poll `PDFOperationControl`. `cancel()` records the request and the terminal
snapshot reports `Cancelled` even if a callback returns normally afterward.
`cancellationLatencyMs` and the trace event are the measurement points for
the cancellation-latency benchmark.

Queued cancellation is finalized without invoking the callback. A running
callback must poll `PDFJobContext::isCancellationRequested()` or pass
`operationControl()` to the underlying Core operation and abandon any partial
result.

## Revision freshness

The document authority should call `setCurrentRevision(documentKey,
revision)` whenever the accepted document revision changes. A queued or
running job whose revision no longer matches is finalized as `Stale` when its
policy is `Discard`; the callback is not run for a stale queued job. This keeps
late tile, overlay, preflight, OCR, and export results from being presented
for a newer document revision.

Each `PDFJobTraceEvent` carries the job's `PDFJobKind` alongside its status,
priority, and timing, so a trace consumer can attribute time to preflight,
OCR, rendering, and the other kinds without re-joining against the original
`PDFJobSpec` (issue #144 AC7).

## Interactive-thread boundary

Issue #144 draws the line between what pointer handlers and frame callbacks
may do directly and what must go through a submitted job. Allowed on the
thread that owns input and frame callbacks: input normalization, small state
transitions, frame scheduling, overlay composition, and applying an already-
computed, bounded result. Not allowed there, even transitively: preflight,
OCR, AI, PDF parsing, filesystem or network access, metadata/font scans, and
unbounded image work -- these are exactly the `PDFJobKind` values a job
carries, and every one of them belongs behind `PDFJobScheduler::submit()`.

`pdf::PDFBlockingThreadGuard` gives that boundary a runtime check instead of
leaving it as a convention. A host with an interactive canvas (`EditorHost`)
registers its owning thread once, at construction, with
`registerInteractiveThread()`. A blocking service adapter -- the entry point
a job's work callback calls into, such as `PreflightEngine::run()` -- opens
with `PDFBlockingThreadGuard::assertOffInteractiveThread(name)` and folds a
`false` return into its own typed error result rather than doing the blocking
work. A tool with no interactive thread (PdfTool, Fuzz, CLI tests) never
registers one, so the guard is a no-op there: it has nothing to protect.
`UnitTestsBlockingThreadGuard` covers the guard directly; `UnitTestsPreflightEngine`
covers the `PreflightEngine::run()` integration.

## Migration inventory

The scheduler contract is landed in Core. Callers migrate onto `PDFJobScheduler`
with document-revision binding. Inventory:

| Work | Existing owner | Scheduler kind | Default priority | Status |
| --- | --- | --- | --- | --- |
| Page and overlay rendering | `LoopLibQuick`, `LoopLibCore` | `Rendering` | `VisiblePage` | **page compile and text layout migrated**; remaining overlay tiles stay on `PDFExecutionPolicy` |
| Preflight and fixups | Editor / PdfTool | `Preflight` or `Export` | `Operator` | **PdfTool `preflight` and Editor preflight migrated** |
| OCR and indexing | Editor plugins / Core | `OCR` | `Background` | remaining (out of S05 scope) |
| PageMaster export | `PdfTool`, `LoopLibCore` | `Export` | `Operator` | **migrated** |
| Thumbnail generation | `LoopLibQuick`, `LoopLibCore` | `Thumbnail` | `NearViewport` | **migrated** |
| PageMaster preview | `PdfTool`, `LoopLibCore` | `Rendering` | `NearViewport` | **migrated** (revision-fenced via `PDFJobScheduler`) |
| Batch analysis | PageMaster / PdfTool | `Batch` | `Background` | remaining (out of S05 scope) |
| Agent context work | future agent surface | `Agent` | `Agent` | remaining |

This migration boundary is deliberate: the scheduler provides the shared
arbitration contract, while subsequent caller changes must preserve each
surface's typed result and UI lifecycle. A source audit can use the table to
reject new unmanaged long-running work and to track the remaining conversions.

`scripts/ci/check_unmanaged_async.py` is the CI guard for that boundary. It
reports zero legacy product `QtConcurrent::run` call sites after Session 10
(`PDFDiff` migrated onto `PDFJobScheduler`) and fails on any new or multiplied
unmanaged launch.

## Verification

`UnitTestsJobScheduler` covers stable priority ordering, terminal
cancellation (including Export/Preflight operator jobs), measured cancellation
latency, stale-revision discard, progress, metadata, and trace visibility.
`PageMasterExportTest::cancel_midOutput_beforeWrite_writesNothing` submits
export through `PDFJobScheduler` and asserts the snapshot is not `Succeeded`.
