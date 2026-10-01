#if defined(__APPLE__)
#include <mach/mach.h>
#endif

extern "C" int32_t elisa_application_v1_activate_render_path(void* render_path) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    service.host.wicked().ActivatePath(static_cast<wi::RenderPath*>(render_path));
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_register_shutdown_hook(
    void* context, elisa_application_shutdown_fn function) {
    if (context == nullptr || function == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    return service.host.add_shutdown_hook(context, function)
        ? ELISA_APPLICATION_OK : ELISA_APPLICATION_INITIALIZATION_FAILED;
}

// Non-deterministic gameplay seed, available before opening a window.
extern "C" uint64_t elisa_application_v1_random_seed(void) {
    try {
        std::random_device source;
        return (static_cast<uint64_t>(source()) << 32) ^ source();
    } catch (...) {
        return static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }
}

// Live memory sample for budget checks: GPU bytes the device has allocated and
// its recommended budget, plus the process's physical footprint (CPU memory).
// Unlike the startup backend profile, each call queries the device again.
extern "C" int32_t elisa_application_v1_memory_usage(
    int64_t* gpu_usage, int64_t* gpu_budget, int64_t* cpu_footprint) {
    if (gpu_usage == nullptr || gpu_budget == nullptr || cpu_footprint == nullptr) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return ELISA_APPLICATION_PROFILE_UNAVAILABLE;
    const auto memory = device->GetMemoryUsage();
    constexpr uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return ELISA_APPLICATION_PROFILE_UNAVAILABLE;
    }
    const uint64_t footprint = info.phys_footprint;
#else
    const uint64_t footprint = wi::helper::GetMemoryUsage().process_physical;
#endif
    if (memory.usage > limit || memory.budget > limit || footprint > limit || footprint == 0) {
        return ELISA_APPLICATION_PROFILE_UNAVAILABLE;
    }
    *gpu_usage = static_cast<int64_t>(memory.usage);
    *gpu_budget = static_cast<int64_t>(memory.budget);
    *cpu_footprint = static_cast<int64_t>(footprint);
    return ELISA_APPLICATION_OK;
}
