#pragma once

// Headless checks for SerialJobWorker, the thread behind asynchronous
// snapshot asset requests. The render-scene smoke covers the same worker
// through RenderScene; these checks cover ordering, bounds, every
// cancellation point, throwing jobs and shutdown with a parked job.
#include "probe_core.h"
#include "snapshot_asset_worker.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace probe {

struct WorkerProbeResult {
    int value = -1;
};

inline bool probe_snapshot_asset_worker() {
    using Worker = elisa::assets::SerialJobWorker<WorkerProbeResult>;
    using Results = std::vector<std::pair<uint64_t, WorkerProbeResult>>;
    const auto patience = std::chrono::seconds(30);
    Worker worker;
    auto value_job = [](int value) { return [value] { return WorkerProbeResult{value}; }; };

    Results ordered;
    const bool ordering = worker.submit(1, value_job(10)) && worker.submit(2, value_job(20)) &&
        worker.wait_until(2, 2, patience) && worker.take(1, ordered) == 1 &&
        worker.take(8, ordered) == 1 && ordered.size() == 2 &&
        ordered[0].first == 1 && ordered[0].second.value == 10 &&
        ordered[1].first == 2 && ordered[1].second.value == 20;

    worker.hold(true, false);
    size_t accepted = 0;
    for (uint64_t serial = 100; serial < 100 + Worker::MAX_JOBS; ++serial) {
        accepted += worker.submit(serial, value_job(1)) ? 1 : 0;
    }
    const bool bounded = accepted == Worker::MAX_JOBS && !worker.submit(500, value_job(1));
    for (uint64_t serial = 100; serial < 100 + Worker::MAX_JOBS; ++serial) worker.cancel(serial);
    const bool drained = worker.outstanding() == 0;

    // A queued serial can't be submitted again, and a cancelled queued job
    // never starts.
    const uint64_t started = worker.started();
    const bool queued = worker.submit(3, value_job(30)) && !worker.submit(3, value_job(31)) &&
        !worker.submit(0, value_job(1)) && worker.cancel(3) && !worker.cancel(3);
    worker.hold(false, false);
    Results after_queued;
    const bool queued_skipped = queued && worker.submit(4, value_job(40)) &&
        worker.wait_until(started + 1, 3, patience) && worker.started() == started + 1 &&
        worker.take(8, after_queued) == 1 && after_queued[0].first == 4;

    // A job cancelled while parked at its checkpoint skips its remaining
    // reads, and its result is dropped.
    std::atomic<int> continued = -1;
    worker.hold(false, true);
    const bool parked = worker.submit(5, [&] {
        continued = worker.checkpoint() ? 1 : 0;
        return WorkerProbeResult{50};
    }) && worker.wait_until(started + 2, 3, patience) && worker.cancel(5);
    worker.hold(false, false);
    Results after_running;
    const bool running_dropped = parked && worker.wait_until(started + 2, 4, patience) &&
        continued == 0 && worker.take(8, after_running) == 0;

    // A finished result is removed before the owner takes it.
    Results after_finished;
    const bool finished_dropped = worker.submit(6, value_job(60)) &&
        worker.wait_until(started + 3, 5, patience) && worker.cancel(6) && !worker.cancel(6) &&
        worker.take(8, after_finished) == 0;

    Results thrown;
    const bool throwing = worker.submit(7, []() -> WorkerProbeResult { throw 1; }) &&
        worker.wait_until(started + 4, 6, patience) && worker.take(8, thrown) == 1 &&
        thrown[0].first == 7 && thrown[0].second.value == -1;

    // Shutdown releases a parked job, discards it and joins; a queued job
    // never starts. The worker then accepts new jobs.
    std::atomic<int> released = -1;
    worker.hold(false, true);
    const bool parked_again = worker.submit(8, [&] {
        released = worker.checkpoint() ? 1 : 0;
        return WorkerProbeResult{80};
    }) && worker.wait_until(started + 5, 6, patience);
    worker.hold(true, true);
    const bool queued_at_stop = worker.submit(9, value_job(90));
    const uint64_t started_before_stop = worker.started();
    worker.stop();
    Results after_stop;
    const bool stopped = parked_again && queued_at_stop && released == 0 &&
        worker.started() == started_before_stop && worker.outstanding() == 0 &&
        worker.take(8, after_stop) == 0;
    Results restarted;
    const bool restart = worker.submit(9, value_job(91)) &&
        worker.wait_until(started_before_stop + 1, 8, patience) &&
        worker.take(8, restarted) == 1 && restarted[0].second.value == 91;

    return check(ordering, "asset worker returns results in completion order") &&
        check(bounded && drained, "asset worker bounds its queue") &&
        check(queued_skipped, "asset worker rejects repeated serials and never starts a cancelled job") &&
        check(running_dropped, "asset worker drops a job cancelled at its checkpoint") &&
        check(finished_dropped, "asset worker removes a cancelled finished result") &&
        check(throwing, "asset worker posts a default result for a throwing job") &&
        check(stopped, "asset worker stop releases, discards and joins a parked job") &&
        check(restart, "asset worker accepts jobs after stop");
}

} // namespace probe
