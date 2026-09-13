// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef PDFLOGSCRUBBER_H
#define PDFLOGSCRUBBER_H

#include "pdfglobal.h"

#include <QString>

namespace pdf
{

/// Scrubs personally identifying information out of text before it is written
/// to the rotating log or a diagnostics bundle. pdf::PDFLogSession applies this
/// unconditionally inside the installed message handler, so no call site can
/// leak PII into a log line by forgetting to scrub - scrubbing is a property of
/// the sink, not a convention callers have to remember.
///
/// The single most important design decision here: a scrubbed absolute path
/// keeps only its extension and drops the basename entirely
/// (`<PATH:.pdf>`, never `<PATH:Acme_Q3_Contract.pdf>`). In this product the
/// filename is usually the PII - customers name documents after the people,
/// companies, and matters they concern - while the extension is the only part
/// of the path with diagnostic value.
class LOOPLIBCORESHARED_EXPORT PDFLogScrubber
{
public:
    PDFLogScrubber() = delete;

    /// Scrubs \p text of the home and temp directories, the login name, the
    /// machine host name, credential material (URL userinfo such as a Sentry
    /// DSN, HTTP authorization values, and secret-named key/value pairs), any
    /// remaining absolute path (Windows, UNC, or POSIX), email addresses, and
    /// IPv4/IPv6 literals. Order matters: the home/temp directory and login
    /// name/host name passes run first so a leftover absolute path outside
    /// those roots is still caught by the generic path pass, and the credential
    /// pass runs before the email/path passes so a DSN is reported as a leaked
    /// secret (`<CREDENTIAL>`) rather than as user data (`<EMAIL>`). Applying
    /// scrub() to already-scrubbed text is a no-op.
    /// \param text Text to scrub
    static QString scrub(const QString& text);
};

}   // namespace pdf

#endif   // PDFLOGSCRUBBER_H
