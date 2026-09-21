// ThreadSanitizer harness for SerialJobWorker, the thread behind asynchronous
// snapshot asset requests. It links only the standard library, so it runs
// under ThreadSanitizer without a window or a graphics device.
#include "snapshot_asset_worker_probe.h"

int main() {
    return probe::probe_snapshot_asset_worker() ? 0 : 1;
}
