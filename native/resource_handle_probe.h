#pragma once

#include "probe_core.h"
#include "resource_handles.h"

#include <cstdio>

namespace probe {

inline bool probe_native_resource_handles(wi::scene::Scene& scene) {
    NativeResourceRegistry resource_registry(scene);
    const NativeResourceHandle handle_probe = resource_registry.create_cube("elisa_handle_probe");
    if (!check(resource_registry.is_live(handle_probe), "native handle creation") ||
        !check(resource_registry.resolve(handle_probe) != wi::ecs::INVALID_ENTITY, "native handle resolution")) {
        return false;
    }
    const wi::ecs::Entity handle_entity = resource_registry.resolve(handle_probe);
    if (!check(resource_registry.destroy_deferred(handle_probe), "native handle logical destruction") ||
        !check(!resource_registry.is_live(handle_probe), "stale native handle rejection") ||
        !check(resource_registry.pending_retirements() == 1, "native handle retirement queued") ||
        !check(scene.objects.GetComponent(handle_entity) != nullptr, "native handle waits for retirement")) {
        return false;
    }
    const NativeResourceHandle reused_handle = resource_registry.create_cube("elisa_handle_reused");
    if (!check(reused_handle.slot != handle_probe.slot, "native handle retirement protects slot") ||
        !check(resource_registry.destroy(reused_handle), "reused native handle destruction")) {
        return false;
    }
    resource_registry.collect_retired();
    if (!check(resource_registry.pending_retirements() == 0, "native handle retirement collected") ||
        !check(scene.objects.GetComponent(handle_entity) == nullptr, "native handle resource removed")) {
        return false;
    }
    const NativeResourceHandle reclaimed = resource_registry.create_cube("elisa_handle_reclaimed");
    if (!check(reclaimed.slot == handle_probe.slot && reclaimed.generation != handle_probe.generation,
            "native handle generation increment") ||
        !check(resource_registry.destroy(reclaimed), "reclaimed native handle destruction")) {
        return false;
    }
    wi::scene::Scene other_scene;
    NativeResourceRegistry other_registry(other_scene);
    return check(!other_registry.is_live(handle_probe), "cross-scene native handle rejection");
}

} // namespace probe
