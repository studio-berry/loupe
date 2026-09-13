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
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef PDFBLOCKINGTHREADGUARD_H
#define PDFBLOCKINGTHREADGUARD_H

#include "pdfglobal.h"

namespace pdf
{

/// Marks the thread that owns pointer handlers and frame callbacks so a
/// blocking service adapter (preflight, OCR, font enumeration, parsing,
/// filesystem/network I/O) can refuse to run there (issue #144).
///
/// Registration is opt-in and global rather than per-object: a host with an
/// interactive canvas (Editor) registers once, early, from that thread. A
/// tool with no such thread (PdfTool, Fuzz, CLI tests) never registers, and
/// the guard is then a no-op that never blocks a caller: there is nothing to
/// protect.
class LOOPLIBCORESHARED_EXPORT PDFBlockingThreadGuard
{
public:
    /// Registers the calling thread as the one interactive input and frame
    /// callbacks run on.
    static void registerInteractiveThread();

    /// Drops the registration. A guard with no registered thread never
    /// reports a violation.
    static void clearInteractiveThread();

    static bool isInteractiveThreadRegistered();

    /// True when called from the registered interactive thread. Always false
    /// when no thread is registered.
    static bool isCurrentThreadInteractive();

    /// A blocking service adapter calls this once, before doing any work.
    /// Returns true when it is safe to proceed. Returns false, and logs a
    /// warning naming \p serviceName, when called from the registered
    /// interactive thread -- the caller must not run the blocking work and
    /// should fold this into its own typed error result instead.
    static bool assertOffInteractiveThread(const char* serviceName);
};

}   // namespace pdf

#endif   // PDFBLOCKINGTHREADGUARD_H
