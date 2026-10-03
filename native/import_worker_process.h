#pragma once

// Runs one untrusted import in a forked child so a crash, hang or runaway
// allocation cannot take down the caller. The child reports through a pipe;
// the parent enforces a wall-clock deadline, kills on expiry and turns every
// outcome into a reproducible diagnostic. POSIX only.
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

#include <poll.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace elisa::assets {

enum class ImportWorkerOutcome { Imported, Refused, Crashed, TimedOut, SpawnFailed };

struct ImportWorkerResult {
    ImportWorkerOutcome outcome = ImportWorkerOutcome::SpawnFailed;
    std::string payload;     // the child's result text, or its refusal reason
    std::string diagnostic;  // one stable line naming the outcome
    int signal = 0;
};

// The job returns true with its result in `out`, or false with a reason.
using ImportWorkerJob = std::function<bool(std::string& out)>;

inline constexpr size_t MAX_IMPORT_WORKER_REPLY = 1u << 20;

inline const char* import_worker_outcome_name(ImportWorkerOutcome outcome) {
    switch (outcome) {
        case ImportWorkerOutcome::Imported: return "imported";
        case ImportWorkerOutcome::Refused: return "refused";
        case ImportWorkerOutcome::Crashed: return "crashed";
        case ImportWorkerOutcome::TimedOut: return "timed out";
        default: return "spawn failed";
    }
}

inline ImportWorkerResult finish_import_worker(ImportWorkerResult result, const std::string& label) {
    result.diagnostic = label + ": " + import_worker_outcome_name(result.outcome);
    if (result.outcome == ImportWorkerOutcome::Crashed) result.diagnostic += " (signal " + std::to_string(result.signal) + ")";
    if (result.outcome == ImportWorkerOutcome::Refused) result.diagnostic += " (" + result.payload + ")";
    return result;
}

// Reply framing: one status byte ('I' or 'R') followed by the text.
inline ImportWorkerResult run_import_worker(const std::string& label, const ImportWorkerJob& job,
        std::chrono::milliseconds deadline, uint64_t memory_limit_bytes = 0) {
    ImportWorkerResult result;
    int fds[2];
    if (pipe(fds) != 0) return finish_import_worker(result, label);
    const pid_t child = fork();
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        return finish_import_worker(result, label);
    }
    if (child == 0) {
        close(fds[0]);
        if (memory_limit_bytes > 0) {
            const rlimit limit{memory_limit_bytes, memory_limit_bytes};
            setrlimit(RLIMIT_DATA, &limit);
        }
        std::string out;
        bool ok = false;
        try {
            ok = job(out);
        } catch (...) {
            ok = false;
            out = "import threw";
        }
        if (out.size() > MAX_IMPORT_WORKER_REPLY) out.resize(MAX_IMPORT_WORKER_REPLY);
        const std::string reply = std::string(1, ok ? 'I' : 'R') + out;
        size_t written = 0;
        while (written < reply.size()) {
            const ssize_t n = write(fds[1], reply.data() + written, reply.size() - written);
            if (n <= 0) break;
            written += static_cast<size_t>(n);
        }
        _exit(0);
    }
    close(fds[1]);
    std::string reply;
    bool timed_out = false;
    const auto end = std::chrono::steady_clock::now() + deadline;
    char buffer[4096];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            timed_out = true;
            break;
        }
        pollfd entry{fds[0], POLLIN, 0};
        const int ready = poll(&entry, 1, static_cast<int>(left.count()));
        if (ready < 0 && errno == EINTR) continue;
        if (ready == 0) {
            timed_out = true;
            break;
        }
        const ssize_t n = read(fds[0], buffer, sizeof buffer);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (reply.size() <= MAX_IMPORT_WORKER_REPLY) reply.append(buffer, static_cast<size_t>(n));
    }
    close(fds[0]);
    if (timed_out) kill(child, SIGKILL);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (timed_out) {
        result.outcome = ImportWorkerOutcome::TimedOut;
    } else if (WIFSIGNALED(status)) {
        result.outcome = ImportWorkerOutcome::Crashed;
        result.signal = WTERMSIG(status);
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || reply.empty() ||
            (reply[0] != 'I' && reply[0] != 'R')) {
        result.outcome = ImportWorkerOutcome::Crashed;
    } else {
        result.outcome = reply[0] == 'I' ? ImportWorkerOutcome::Imported : ImportWorkerOutcome::Refused;
        result.payload = reply.substr(1);
    }
    return finish_import_worker(result, label);
}

} // namespace elisa::assets
