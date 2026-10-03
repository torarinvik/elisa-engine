#include "application_abi.h"

extern "C" int32_t elisa_application_v1_test_set_window_size(int32_t width, int32_t height);
extern "C" void elisa_application_v1_test_trace_stress(int32_t iteration, int64_t instances,
    int64_t navmeshes, int64_t voices, int64_t streams, int64_t heap_bytes);

int main() {
    if (elisa_application_v1_test_set_window_size(640, 480) != ELISA_APPLICATION_UNSUPPORTED)
        return 1;
    elisa_application_v1_test_trace_stress(1, 0, 0, 0, 0, 0);
    return 0;
}
