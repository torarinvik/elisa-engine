#pragma once
// Local crash artifacts. `install` pre-formats everything that does not
// depend on the fault (build identity, launch arguments, main-image load
// address) so the signal handler only calls write/open/backtrace and then
// re-raises with the default action, keeping the process's exit signal.
// No network, no telemetry: the file stays in the configured directory.

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif

namespace elisa::crash {

constexpr size_t PATH_CAPACITY = 1024;
constexpr size_t PREAMBLE_CAPACITY = 8192;
constexpr int MAX_FRAMES = 64;
constexpr size_t ALTERNATE_STACK_BYTES = 64 * 1024;

struct CrashState {
    char path[PATH_CAPACITY] = {};
    char preamble[PREAMBLE_CAPACITY] = {};
    size_t preamble_length = 0;
    bool installed = false;
};

inline CrashState& crash_state() {
    static CrashState state;
    return state;
}

inline void append(char* buffer, size_t capacity, size_t& length, const char* text) {
    while (text != nullptr && *text != '\0' && length + 1 < capacity) buffer[length++] = *text++;
    buffer[length] = '\0';
}

inline void append_hex(char* buffer, size_t capacity, size_t& length, uintptr_t value) {
    char digits[2 + sizeof(uintptr_t) * 2 + 1];
    digits[0] = '0';
    digits[1] = 'x';
    for (size_t index = 0; index < sizeof(uintptr_t) * 2; ++index) {
        const unsigned nibble = unsigned(value >> ((sizeof(uintptr_t) * 2 - 1 - index) * 4)) & 15u;
        digits[2 + index] = char(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    digits[sizeof(digits) - 1] = '\0';
    append(buffer, capacity, length, digits);
}

inline const char* signal_name(int signal_number) {
    switch (signal_number) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    case SIGTRAP: return "SIGTRAP";
    default: return "signal";
    }
}

inline const int* handled_signals(size_t& count) {
    static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
    count = sizeof(signals) / sizeof(signals[0]);
    return signals;
}

inline void write_all(int descriptor, const char* text, size_t length) {
    while (length > 0) {
        const ssize_t written = ::write(descriptor, text, length);
        if (written <= 0) {
            if (written < 0 && errno == EINTR) continue;
            return;
        }
        text += written;
        length -= size_t(written);
    }
}

inline void handle_crash(int signal_number, siginfo_t* info, void*) {
    CrashState& state = crash_state();
    const int descriptor = ::open(state.path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor >= 0) {
        char line[160];
        size_t length = 0;
        line[0] = '\0';
        append(line, sizeof(line), length, "signal: ");
        append(line, sizeof(line), length, signal_name(signal_number));
        append(line, sizeof(line), length, "\nfault_address: ");
        append_hex(line, sizeof(line), length, reinterpret_cast<uintptr_t>(info ? info->si_addr : nullptr));
        append(line, sizeof(line), length, "\n");
        write_all(descriptor, line, length);
        write_all(descriptor, state.preamble, state.preamble_length);
        static const char frames_header[] = "frames:\n";
        write_all(descriptor, frames_header, sizeof(frames_header) - 1);
        void* frames[MAX_FRAMES];
        const int count = ::backtrace(frames, MAX_FRAMES);
        ::backtrace_symbols_fd(frames, count, descriptor);
        ::close(descriptor);
    }
    ::signal(signal_number, SIG_DFL);
    ::raise(signal_number);
}

// Returns false (and installs nothing) for an unusable directory or when a
// report path would not fit. `identity` names the build; may be empty.
inline bool install(const char* directory, const char* identity) {
    CrashState& state = crash_state();
    if (state.installed || directory == nullptr || directory[0] == '\0') return false;
    char path[PATH_CAPACITY];
    const int written = std::snprintf(path, sizeof(path), "%s/crash-%d.txt", directory, int(::getpid()));
    if (written <= 0 || size_t(written) >= sizeof(path)) return false;
    if (::access(directory, W_OK) != 0) return false;
    std::memcpy(state.path, path, size_t(written) + 1);
    size_t length = 0;
    char* preamble = state.preamble;
    append(preamble, PREAMBLE_CAPACITY, length, "build: ");
    append(preamble, PREAMBLE_CAPACITY, length, identity != nullptr && identity[0] != '\0' ? identity : "(unset)");
    append(preamble, PREAMBLE_CAPACITY, length, "\n");
#if defined(__APPLE__)
    append(preamble, PREAMBLE_CAPACITY, length, "image: ");
    append(preamble, PREAMBLE_CAPACITY, length, _dyld_get_image_name(0));
    append(preamble, PREAMBLE_CAPACITY, length, "\nload_address: ");
    append_hex(preamble, PREAMBLE_CAPACITY, length, reinterpret_cast<uintptr_t>(_dyld_get_image_header(0)));
    append(preamble, PREAMBLE_CAPACITY, length, "\narguments:");
    char** arguments = *_NSGetArgv();
    const int argument_count = *_NSGetArgc();
    for (int index = 0; index < argument_count; ++index) {
        append(preamble, PREAMBLE_CAPACITY, length, " [");
        append(preamble, PREAMBLE_CAPACITY, length, arguments[index]);
        append(preamble, PREAMBLE_CAPACITY, length, "]");
    }
    append(preamble, PREAMBLE_CAPACITY, length, "\n");
#endif
    state.preamble_length = length;
    static char alternate_stack[ALTERNATE_STACK_BYTES];
    stack_t stack = {};
    stack.ss_sp = alternate_stack;
    stack.ss_size = sizeof(alternate_stack);
    if (::sigaltstack(&stack, nullptr) != 0) return false;
    struct sigaction action = {};
    action.sa_sigaction = handle_crash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    size_t count = 0;
    const int* signals = handled_signals(count);
    for (size_t index = 0; index < count; ++index) {
        if (::sigaction(signals[index], &action, nullptr) != 0) return false;
    }
    // Warm backtrace() so its first use is not inside the handler.
    void* warm[1];
    (void)::backtrace(warm, 1);
    state.installed = true;
    return true;
}

// Reads ELISA_CRASH_DIR and ELISA_BUILD_IDENTITY; a missing directory means
// crash reports are off.
inline bool install_from_environment() {
    return install(std::getenv("ELISA_CRASH_DIR"), std::getenv("ELISA_BUILD_IDENTITY"));
}

}  // namespace elisa::crash
