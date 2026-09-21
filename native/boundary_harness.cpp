// Sanitizer harness for the untrusted-boundary libraries. The full graphics
// probe links Wicked and needs a display session; this harness exercises the
// same library checks (ozz sampling, Recast/Detour navigation, miniaudio decode,
// FreeType/HarfBuzz shaping, the asset worker) without a window, so
// AddressSanitizer and UndefinedBehaviorSanitizer can run on the unverified
// boundary even where the instrumented graphics run is not permitted. It links
// no Wicked library; it only shares the probe headers.
#include "miniaudio_probe.h"
#include "ozz_probe.h"
#include "recast_probe.h"
#include "text_probe.h"
#include "udp_probe.h"
#include "menu_probe.h"
#include "pose_probe.h"
#include "snapshot_asset_worker_probe.h"

#include <cstdio>
#include <map>
#include <string>

int main() {
    const bool ozz = probe::probe_ozz_sampling();
    const bool recast = probe::probe_recast_navigation();
    const bool audio = probe::probe_miniaudio();
    const bool text = probe::probe_text();
    const bool udp = probe::probe_udp_loopback();
    const bool worker = probe::probe_snapshot_asset_worker();
    std::map<std::string, std::string> manifest;
    const bool manifest_ok = probe::load_manifest("backends/scene_manifest.txt", manifest);
    const bool extra = !manifest_ok || (probe::probe_menu(manifest) && probe::probe_pose(manifest));
    std::fprintf(stdout, "boundary harness: ozz=%d recast=%d audio=%d text=%d udp=%d worker=%d menu_and_pose=%d\n",
        ozz ? 1 : 0, recast ? 1 : 0, audio ? 1 : 0, text ? 1 : 0, udp ? 1 : 0, worker ? 1 : 0,
        extra ? 1 : 0);
    return (ozz && recast && audio && text && udp && worker && extra) ? 0 : 1;
}
