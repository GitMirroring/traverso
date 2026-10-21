// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TIOScheduler with streams and tasks of its own, no core,
// no driver: dispatch() called directly, a stream removed while rounds run
// on the loop thread, the end with tasks still running, a seek, a new rate.

#include "TIOScheduler.h"
#include "TIOStream.h"
#include "TIOTask.h"

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <functional>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// A stream that counts what the scheduler does with it
class FakeStream final : public TIOStream
{
public:
    explicit FakeStream(bool needsWork, int fill = 100, int stepMicroseconds = 0)
        : m_needsWork(needsWork), m_fill(fill), m_stepMicroseconds(stepMicroseconds) {}

    TIOPlanner::Stream disk_state(const TTimeRef&) override { return {m_needsWork, true, m_fill}; }
    int step(const StepInfo& info) override
    {
        if (m_retired.load()) {
            m_stepsAfterRetire.fetch_add(1);
        }
        if (m_stepMicroseconds > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(m_stepMicroseconds));
        }
        m_lastTransport.store(info.transportLocation.universal_frame());
        if (info.seek) {
            m_lastSeek.store(info.seekLocation.universal_frame());
            if (m_onSeek) {
                m_onSeek();
            }
            m_seeks.fetch_add(1);
            if (info.sampleRateChanged) {
                m_rateChanges.fetch_add(1);
                m_rate.store(info.outputRate);
            }
        }
        m_steps.fetch_add(1);
        return m_fill;
    }
    void set_output_rate_and_convertor_type(int outputRate, int) override { m_rate.store(uint(outputRate)); }
    uint get_channel_count() const override { return 1; }
    void prepare_rt_buffers(nframes_t) override { m_prepared.fetch_add(1); }
    void retire() override { m_retired.fetch_add(1); }

    bool m_needsWork;
    int m_fill;
    int m_stepMicroseconds;
    std::atomic<int> m_steps{0};
    std::atomic<int> m_seeks{0};
    std::atomic<int> m_rateChanges{0};
    std::atomic<uint> m_rate{0};
    std::atomic<int> m_prepared{0};
    std::atomic<int> m_retired{0};
    std::atomic<int> m_stepsAfterRetire{0};
    std::atomic<qint64> m_lastTransport{0};
    std::atomic<qint64> m_lastSeek{0};
    std::function<void()> m_onSeek;
};

// A task of a number of steps, sleeping a while in each; it counts into
// numbers of the test, as the scheduler deletes it
class FakeTask final : public TIOTask
{
public:
    FakeTask(int steps, std::atomic<int>& retired, std::atomic<int>& done, int stepMicroseconds = 0)
        : m_left(steps), m_retired(retired), m_done(done), m_stepMicroseconds(stepMicroseconds) {}

    bool step() override
    {
        if (m_stepMicroseconds > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(m_stepMicroseconds));
        }
        m_done.fetch_add(1);
        return --m_left > 0;
    }
    void retire() override { m_retired.fetch_add(1); }

    int m_left;
    std::atomic<int>& m_retired;
    std::atomic<int>& m_done;
    int m_stepMicroseconds;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // dispatch() without a loop and without a driver: the streams that need
    // work get one step per round, an idle task runs to its end and is
    // handed back once
    {
        TIOScheduler scheduler;
        scheduler.set_buffer_size(256);
        FakeStream reading(true, 50);
        FakeStream full(false, 100);
        scheduler.add_stream(&reading);
        scheduler.add_stream(&full);
        check(reading.m_prepared.load() == 1 && full.m_prepared.load() == 1, "add_stream() makes the ring buffers");
        check(!scheduler.dispatch(), "only streams: the next round waits for a wake");
        check(reading.m_steps.load() == 1 && full.m_steps.load() == 0, "one step for the stream that needs work");

        reading.m_needsWork = false;
        reading.m_fill = 100;
        std::atomic<int> retired{0};
        std::atomic<int> done{0};
        scheduler.add_idle_task(std::make_unique<FakeTask>(3, retired, done));
        int rounds = 0;
        while (scheduler.dispatch() && rounds < 10) {
            ++rounds;
        }
        check(retired.load() == 1, "the task is handed back once when it is over");
        check(done.load() == 3, "the task took its three steps");

        scheduler.remove_stream(&reading);
        scheduler.remove_stream(&full);
        check(reading.m_retired.load() == 1 && full.m_retired.load() == 1, "remove_stream() without a loop hands the stream back");
        reading.m_needsWork = true;
        scheduler.dispatch();
        check(reading.m_steps.load() == 1, "a removed stream gets no step");
    }

    // A stream removed while the loop runs rounds (the audio thread ticks):
    // remove_stream() returns only once the loop let go of it, no step of it
    // runs after that
    for (int round = 0; round < 20; ++round) {
        TIOScheduler scheduler;
        scheduler.set_buffer_size(256);
        FakeStream removed(true, 50, 200);
        FakeStream stays(true, 50, 200);
        scheduler.add_stream(&removed);
        scheduler.add_stream(&stays);
        scheduler.start();
        std::atomic<bool> ticking{true};
        std::thread audio([&] {
            while (ticking.load()) {
                scheduler.tick();
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });
        while (removed.m_steps.load() < 3) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        scheduler.remove_stream(&removed);
        const int stepsAtRemoval = removed.m_steps.load();
        check(removed.m_retired.load() == 1, "the removed stream is handed back before remove_stream() returns");
        const int staysBefore = stays.m_steps.load();
        while (stays.m_steps.load() < staysBefore + 5) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        check(removed.m_steps.load() == stepsAtRemoval && removed.m_stepsAfterRetire.load() == 0,
              "no step of a removed stream after remove_stream()");
        ticking.store(false);
        audio.join();
        scheduler.remove_stream(&stays);
    }

    // The end while tasks run: the round that runs ends first, every task
    // that is left is handed back once
    {
        std::atomic<int> retired{0};
        std::atomic<int> done{0};
        {
            TIOScheduler scheduler;
            scheduler.set_idle_limit(2);
            for (int i = 0; i < 4; ++i) {
                scheduler.add_idle_task(std::make_unique<FakeTask>(1000, retired, done, 300));
            }
            scheduler.start();
            for (int i = 0; i < 20; ++i) {
                scheduler.tick();
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        }
        check(retired.load() == 4 && done.load() > 0, "every task that was left is handed back once at the end");
    }

    // A seek: one seek step per stream, then seekFinished; a new rate is
    // passed to the next seek only
    {
        TIOScheduler scheduler;
        scheduler.set_buffer_size(256);
        std::atomic<int> finished{0};
        QObject::connect(&scheduler, &TIOScheduler::seekFinished, [&finished] { finished.fetch_add(1); });
        FakeStream a(false, 100);
        FakeStream b(false, 100);
        scheduler.add_stream(&a);
        scheduler.add_stream(&b);
        scheduler.set_output_sample_rate(44100);
        scheduler.set_seek_transport_location(TTimeRef(qint64(1000)));
        scheduler.dispatch();
        check(a.m_seeks.load() == 1 && b.m_seeks.load() == 1 && finished.load() == 1, "a seek steps every stream once");
        check(a.m_rateChanges.load() == 1 && a.m_rate.load() == 44100, "the new rate goes with the seek");
        scheduler.set_seek_transport_location(TTimeRef(qint64(2000)));
        scheduler.dispatch();
        check(a.m_rateChanges.load() == 1 && finished.load() == 2, "a seek without a new rate keeps the rate");
        scheduler.remove_stream(&a);
        scheduler.remove_stream(&b);
    }

    // A seek asked for while a seek round runs is not lost: the next round
    // serves it, and the transport goes on from where the streams seeked to
    {
        TIOScheduler scheduler;
        scheduler.set_buffer_size(256);
        FakeStream a(true, 50);
        scheduler.add_stream(&a);
        a.m_onSeek = [&] {
            if (a.m_seeks.load() == 0) {
                scheduler.set_seek_transport_location(TTimeRef(qint64(5000)));
            }
        };
        scheduler.set_seek_transport_location(TTimeRef(qint64(1000)));
        scheduler.dispatch();
        check(a.m_seeks.load() == 1 && a.m_lastSeek.load() == 1000, "the first seek");
        scheduler.dispatch();
        check(a.m_seeks.load() == 2 && a.m_lastSeek.load() == 5000, "the seek asked for during the round comes next");
        scheduler.dispatch();
        check(a.m_lastTransport.load() == 5000, "the transport goes on from the last seek");
        scheduler.remove_stream(&a);
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
