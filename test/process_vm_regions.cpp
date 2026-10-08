#include "../native/process_vm_regions.h"
#if defined(__APPLE__)
#include <mach/vm_statistics.h>
#include <cstdio>

int main() {
    using namespace elisa::diagnostics;
    VmRegionSnapshot synthetic{};
    if (!add_vm_region(synthetic, 1, 32768, 2, 1, 16384)) return 1;
    if (synthetic.visited != 1 || synthetic.tags[1].resident_bytes != 32768 ||
        synthetic.tags[1].dirtied_bytes != 16384) return 2;
    if (!add_vm_region(synthetic, 900, 16384, 1, 1, 16384) ||
        synthetic.tags[VM_REGION_OTHER_TAG].regions != 1) return 3;
    if (add_vm_region(synthetic, 1, 1, 1, 1, 0) || synthetic.visited != 2) return 4;
    if (add_vm_region(synthetic, 1, 1, UINT64_MAX, 1, 16384) || synthetic.visited != 2) return 5;
    synthetic.tags[1].virtual_bytes = UINT64_MAX;
    if (add_vm_region(synthetic, 1, 1, 1, 1, 16384) || synthetic.visited != 2) return 6;
    synthetic.tags[1].virtual_bytes = 0;
    synthetic.tags[1].resident_bytes = UINT64_MAX;
    if (add_vm_region(synthetic, 1, 1, 1, 0, 16384) || synthetic.visited != 2) return 14;
    synthetic.tags[1].resident_bytes = 0;
    synthetic.tags[1].dirtied_bytes = UINT64_MAX;
    if (add_vm_region(synthetic, 1, 1, 0, 1, 16384) || synthetic.visited != 2) return 15;
    synthetic.tags[1].dirtied_bytes = 0;
    synthetic.tags[1].regions = UINT64_MAX;
    if (add_vm_region(synthetic, 1, 1, 0, 0, 16384) || synthetic.visited != 2) return 16;
    synthetic.visited = VM_REGION_WALK_LIMIT;
    if (add_vm_region(synthetic, 1, 1, 1, 1, 16384)) return 7;

    constexpr unsigned tag = VM_MEMORY_APPLICATION_SPECIFIC_1;
    const VmRegionSnapshot before = process_vm_regions();
    if (!before.complete) return 8;
    constexpr mach_vm_size_t bytes = 4 * 1024 * 1024;
    mach_vm_address_t address = 0;
    if (mach_vm_allocate(mach_task_self(), &address, bytes,
            VM_FLAGS_ANYWHERE | VM_MAKE_TAG(tag)) != KERN_SUCCESS) return 9;
    auto* pages = reinterpret_cast<volatile unsigned char*>(address);
    for (mach_vm_size_t offset = 0; offset < bytes; offset += vm_page_size) pages[offset] = 1;
    const VmRegionSnapshot allocated = process_vm_regions();
    const kern_return_t freed = mach_vm_deallocate(mach_task_self(), address, bytes);
    if (freed != KERN_SUCCESS) return 10;
    const VmRegionSnapshot after = process_vm_regions();
    if (!allocated.complete || !after.complete) return 11;
    if (allocated.tags[tag].virtual_bytes < before.tags[tag].virtual_bytes + bytes ||
        allocated.tags[tag].resident_bytes < before.tags[tag].resident_bytes + bytes) return 12;
    if (after.tags[tag].virtual_bytes != before.tags[tag].virtual_bytes ||
        after.tags[tag].resident_bytes != before.tags[tag].resident_bytes) return 13;
    std::puts("VM regions: tagged allocation/release observed; overflow and bounded aggregation refused");
    return 0;
}
#else
int main() { return 0; }
#endif
