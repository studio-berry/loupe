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

#include "pdfblockingthreadguard.h"

#include <QThread>
#include <QtGlobal>

#include <atomic>

namespace pdf
{

namespace
{

std::atomic<QThread*> s_interactiveThread{ nullptr };

}   // namespace

void PDFBlockingThreadGuard::registerInteractiveThread()
{
    s_interactiveThread.store(QThread::currentThread(), std::memory_order_release);
}

void PDFBlockingThreadGuard::clearInteractiveThread()
{
    s_interactiveThread.store(nullptr, std::memory_order_release);
}

bool PDFBlockingThreadGuard::isInteractiveThreadRegistered()
{
    return s_interactiveThread.load(std::memory_order_acquire) != nullptr;
}

bool PDFBlockingThreadGuard::isCurrentThreadInteractive()
{
    QThread* registered = s_interactiveThread.load(std::memory_order_acquire);
    return registered != nullptr && registered == QThread::currentThread();
}

bool PDFBlockingThreadGuard::assertOffInteractiveThread(const char* serviceName)
{
    if (!isCurrentThreadInteractive())
    {
        return true;
    }

    qWarning("%s must not run on the interactive thread: it would block pointer handling and frame callbacks.",
             serviceName ? serviceName : "A blocking service");
    return false;
}

}   // namespace pdf
