Category: fixed
Audience: developers, users
Breaking-Change: no
Summary: Act on the post-0.2.0 exhaustive read-only review. PDFLogScrubber now scrubs credential
material - URL userinfo (the shape of a Sentry DSN), HTTP authorization values, and secret-named
key/value pairs - before the existing path/email passes, so a leaked token is reported as
<CREDENTIAL> rather than partially eaten by the email pass; the key vocabulary matches
isSensitiveKey() in pdfartifactidentity.cpp and the bare auth-scheme pass excludes "Token" so
parser diagnostics ("Unexpected token appeared") survive. PdfTool's extraction commands
(fetch-images, fetch-text, attachments) now always record an output.empty-result diagnostic when
they produce nothing and accept a shared --fail-if-empty that turns that into exit 1 (findings),
so a pipeline gating on "figures were produced" cannot be green-lit by an empty output directory;
documented in docs/PDFTOOL_CLI_CONTRACT.md. The loop-ocr sidecar reads a staged page raster once
by descriptor (O_NOFOLLOW where available, size-capped, regular-file checked) and passes the bytes
to PIL and easyocr instead of re-resolving the path three times, closing the TOCTOU window;
language codes are shape-validated so a traversal-shaped value cannot reach easyocr's model file
names (mirrored in ocr-sidecar.schema.json), and PdfTool sets the staged raster 0600.
PDFDocumentWriter::writeIncremental reports through an optional IncrementalWriteOutcome whether it
appended or only byte-copied an unchanged document - previously indistinguishable from the success
value alone. Damaged-document recovery bounds its dense object table by how many objects were
actually recovered instead of by the highest object number the document happens to declare, and
now carries a real source digest so writeIncremental's "the file changed underneath us" guard is
not silently disabled for permissively recovered documents. PDFNameTreeLoader bounds traversal:
cyclic Kids chains terminate, nesting and entry count are capped, and over-long keys are refused
instead of stored verbatim in the document model. Structure-tree parsing bounds its recursion
depth (cycles were already refused; long acyclic chains were not). PDFJBIG2Bitmap::paint validates
the grown dimensions on its expandY path, the one place a bitmap grows after construction and so
the one place that escaped the constructor's dimension check. PDFFilenameSanitizer::isPathContained
no longer reports a planned output as escaping simply because its target directory has not been
created yet, while keeping the stricter symlinked-parent rule for the file side.
PDFSafeFileWriter::makeUniqueFileName probes 128 sequential names then switches to a random
discriminator instead of scanning up to 100k candidates. Diagnostics bundles truncate plugin
display fields so an oversized plugin manifest cannot bloat every future support bundle. The OCR
option defaults (languages, dpi, min-text-chars) are defined once and shared between the
capability-discovery table and the command-line parser.
