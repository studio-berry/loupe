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

#include <QtTest>

#include <atomic>
#include <thread>

namespace
{

/// Clears the registration on scope exit regardless of how the test slot
/// returns, so one slot's registration can never leak into the next slot run
/// in the same process.
class ScopedInteractiveThreadRegistration final
{
public:
    ScopedInteractiveThreadRegistration() { pdf::PDFBlockingThreadGuard::registerInteractiveThread(); }
    ~ScopedInteractiveThreadRegistration() { pdf::PDFBlockingThreadGuard::clearInteractiveThread(); }

    ScopedInteractiveThreadRegistration(const ScopedInteractiveThreadRegistration&) = delete;
    ScopedInteractiveThreadRegistration& operator=(const ScopedInteractiveThreadRegistration&) = delete;
};

}   // namespace

class BlockingThreadGuardTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

    void unregisteredGuardNeverTrips();
    void registeredCurrentThreadTripsTheGuard();
    void workerThreadNeverTripsARegisteredGuard();
};

void BlockingThreadGuardTest::cleanup()
{
    // Belt-and-braces: guarantees no slot's registration survives into the
    // next slot even if a slot is added later without the RAII helper.
    pdf::PDFBlockingThreadGuard::clearInteractiveThread();
}

void BlockingThreadGuardTest::unregisteredGuardNeverTrips()
{
    QVERIFY(!pdf::PDFBlockingThreadGuard::isInteractiveThreadRegistered());
    QVERIFY(!pdf::PDFBlockingThreadGuard::isCurrentThreadInteractive());
    QVERIFY(pdf::PDFBlockingThreadGuard::assertOffInteractiveThread("test-service"));
}

void BlockingThreadGuardTest::registeredCurrentThreadTripsTheGuard()
{
    ScopedInteractiveThreadRegistration scoped;

    QVERIFY(pdf::PDFBlockingThreadGuard::isInteractiveThreadRegistered());
    QVERIFY(pdf::PDFBlockingThreadGuard::isCurrentThreadInteractive());

    // The whole point of issue #144's thread-affinity assertion: a blocking
    // service invoked on the registered interactive thread must be caught,
    // not silently allowed to stall pointer/frame handling.
    QVERIFY(!pdf::PDFBlockingThreadGuard::assertOffInteractiveThread("test-service"));
}

void BlockingThreadGuardTest::workerThreadNeverTripsARegisteredGuard()
{
    ScopedInteractiveThreadRegistration scoped;

    std::atomic_bool workerSawItselfAsOffInteractiveThread = false;
    std::thread worker([&workerSawItselfAsOffInteractiveThread]
                       { workerSawItselfAsOffInteractiveThread.store(
                             pdf::PDFBlockingThreadGuard::assertOffInteractiveThread("test-service"),
                             std::memory_order_release); });
    worker.join();

    QVERIFY(workerSawItselfAsOffInteractiveThread.load(std::memory_order_acquire));
}

QTEST_GUILESS_MAIN(BlockingThreadGuardTest)

#include "tst_blockingthreadguardtest.moc"
