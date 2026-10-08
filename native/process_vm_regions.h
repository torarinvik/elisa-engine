#pragma once

// Diagnostic VM map inventory. Resident pages are not physical footprint:
// shared mappings and device accounting have different charging rules.
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <cstdint>
#include <limits>

namespace elisa::diagnostics {
constexpr unsigned VM_REGION_TAG_BUCKETS = 257;
constexpr unsigned VM_REGION_OTHER_TAG = 256;
constexpr unsigned VM_REGION_WALK_LIMIT = 65536;
constexpr unsigned VM_REGION_DEPTH_LIMIT = 64;

struct VmRegionTotals {
    uint64_t virtual_bytes = 0;
    uint64_t resident_bytes = 0;
    uint64_t dirtied_bytes = 0;
    uint64_t regions = 0;
};
struct VmRegionSnapshot {
    VmRegionTotals tags[VM_REGION_TAG_BUCKETS]{};
    unsigned visited = 0;
    bool complete = false;
    kern_return_t status = KERN_SUCCESS;
};

inline bool add_vm_region(VmRegionSnapshot& snapshot, unsigned tag,
    uint64_t size, uint64_t resident_pages, uint64_t dirtied_pages,
    uint64_t page_size) {
    if (page_size == 0 || snapshot.visited >= VM_REGION_WALK_LIMIT) return false;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    if (resident_pages > maximum / page_size || dirtied_pages > maximum / page_size) return false;
    const uint64_t resident = resident_pages * page_size;
    const uint64_t dirtied = dirtied_pages * page_size;
    VmRegionTotals& totals = snapshot.tags[tag < VM_REGION_OTHER_TAG ? tag : VM_REGION_OTHER_TAG];
    if (size > maximum - totals.virtual_bytes || resident > maximum - totals.resident_bytes ||
        dirtied > maximum - totals.dirtied_bytes || totals.regions == maximum) return false;
    totals.virtual_bytes += size;
    totals.resident_bytes += resident;
    totals.dirtied_bytes += dirtied;
    ++totals.regions;
    ++snapshot.visited;
    return true;
}

inline VmRegionSnapshot process_vm_regions() {
    VmRegionSnapshot snapshot{};
    mach_vm_address_t address = 0;
    natural_t depth = 0;
    for (unsigned step = 0; step < VM_REGION_WALK_LIMIT; ++step) {
        mach_vm_size_t size = 0;
        vm_region_submap_info_data_64_t info{};
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
        snapshot.status = mach_vm_region_recurse(mach_task_self(), &address, &size,
            &depth, reinterpret_cast<vm_region_recurse_info_t>(&info), &count);
        if (snapshot.status == KERN_INVALID_ADDRESS) {
            snapshot.complete = true;
            return snapshot;
        }
        if (snapshot.status != KERN_SUCCESS || count < VM_REGION_SUBMAP_INFO_COUNT_64) return snapshot;
        if (info.is_submap) {
            if (depth >= VM_REGION_DEPTH_LIMIT) return snapshot;
            ++depth;
            continue;
        }
        if (size == 0 || size > std::numeric_limits<mach_vm_address_t>::max() - address) return snapshot;
        if (!add_vm_region(snapshot, info.user_tag, size, info.pages_resident,
                info.pages_dirtied, vm_page_size)) return snapshot;
        address += size;
    }
    return snapshot;
}
} // namespace elisa::diagnostics
#endif
