#include "../../ui/src/WorkerThreads.h"
#include <gtest/gtest.h>
#include <atomic>

TEST(WorkerThreads, ShutdownInterruptsJoinsAndReleasesWithoutEventLoop) {
    std::atomic_bool finished = false;
    auto payload = std::make_shared<int>(42);
    std::weak_ptr<int> weak = payload;
    {
        pfui::WorkerThreads workers;
        workers.start(QThread::create([payload, &finished] {
            while (!QThread::currentThread()->isInterruptionRequested())
                QThread::msleep(1);
            finished = true;
        }));
        payload.reset();
    }
    EXPECT_TRUE(finished);
    EXPECT_TRUE(weak.expired());
}
