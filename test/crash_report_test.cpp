// Crash-report regression: a child installs the handler and faults; the
// parent checks it died by the original signal and left a complete report.
#include "crash_report.h"

#include <string>
#include <sys/wait.h>

namespace {

int child(const char* directory, int signal_number) {
    if (!elisa::crash::install(directory, "Test 1.0 (org.elisa.test) sha256=abc rev=def")) return 90;
    if (signal_number == SIGSEGV) {
        volatile int* nothing = nullptr;
        return *nothing;
    }
    ::raise(signal_number);
    return 91;
}

std::string read_file(const std::string& path) {
    std::string text;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return text;
    char buffer[4096];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, read);
    std::fclose(file);
    return text;
}

int crash_case(const char* directory, int signal_number, const char* name, int base) {
    const pid_t pid = ::fork();
    if (pid < 0) return base;
    if (pid == 0) ::_exit(child(directory, signal_number));
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) return base + 1;
    if (!WIFSIGNALED(status) || WTERMSIG(status) != signal_number) return base + 2;
    const std::string report = read_file(std::string(directory) + "/crash-" + std::to_string(pid) + ".txt");
    if (report.find(std::string("signal: ") + name + "\n") != 0) return base + 3;
    if (report.find("build: Test 1.0 (org.elisa.test) sha256=abc rev=def\n") == std::string::npos) return base + 4;
    if (report.find("load_address: 0x") == std::string::npos) return base + 5;
    if (report.find("arguments: [") == std::string::npos || report.find("[--probe-arg]") == std::string::npos) return base + 6;
    // The faulting frame is symbolized from the test binary itself.
    if (report.find("frames:\n") == std::string::npos || report.find("child") == std::string::npos) return base + 7;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[2]) != "--probe-arg") return 2;
    const char* directory = argv[1];
    if (int status = crash_case(directory, SIGSEGV, "SIGSEGV", 10)) return status;
    if (int status = crash_case(directory, SIGABRT, "SIGABRT", 20)) return status;
    // An unwritable or empty directory installs nothing.
    if (elisa::crash::install("/nonexistent-elisa-crash-dir", "x")) return 30;
    if (elisa::crash::install("", "x")) return 31;
    return 0;
}
