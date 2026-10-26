#include <chrono>

#include <stdlib.h>
#include <unistd.h>

#include <string>

#include "TestUtil.h"
#include "core/Logger.h"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    // the tests never write into ~/Library/Logs
    const char* tmp = getenv("TMPDIR");
    std::string logDir = std::string(tmp && *tmp ? tmp : "/tmp") + "/tonetrace_tests_log_XXXXXX";
    const bool hasLogDir = mkdtemp(&logDir[0]) != nullptr;
    if (hasLogDir) ttr::Logger::instance().configure(logDir, 1024 * 1024, 3);
    int run = 0, failed = 0;
    for (const auto& t : tt::registry()) {
        if (filter && std::string(t.name).find(filter) == std::string::npos) continue;
        const int before = tt::failures();
        const auto start = std::chrono::steady_clock::now();
        t.fn();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const bool ok = tt::failures() == before;
        std::printf("[%s] %s (%.0f ms)\n", ok ? " OK " : "FAIL", t.name, ms);
        ++run;
        failed += ok ? 0 : 1;
    }
    ttr::Logger::instance().configure("", 1024 * 1024, 3);
    if (hasLogDir) std::system(("rm -rf '" + logDir + "'").c_str());
    std::printf("%d tests, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
