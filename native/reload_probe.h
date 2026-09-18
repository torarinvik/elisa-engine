#pragma once
// Asset reload on the native host. The plan prioritizes asset reload before
// code reload: the host must be able to release a representation and rebuild it
// from canonical data without changing identity or accumulating resources. The
// probe reloads the cooked package from disk, requires the reloaded geometry to
// match the in-memory copy exactly, builds a mesh from it, and removes it again
// so the scene component count returns to its baseline.
#include "probe_support.h"
#include "package_load.h"

#include <cstdio>
#include <string>

namespace probe {

inline bool probe_reload(wi::scene::Scene& scene, const CookedPackage& original,
    const std::string& package_path) {
    const CookedPackage reloaded = load_cooked_package(package_path);
    if (!check(reloaded.loaded, "reloaded package format") ||
        !check(reloaded.position_data == original.position_data, "reloaded positions match") ||
        !check(reloaded.normal_data == original.normal_data, "reloaded normals match") ||
        !check(reloaded.index_data == original.index_data, "reloaded indices match")) {
        return false;
    }
    const size_t objects_before = scene.objects.GetCount();
    const auto entity = create_cooked_mesh(scene, "elisa_reload_probe", reloaded,
        to_wicked_space(60.0f, 0.0f, 0.0f), 0.1f, XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f));
    if (!check(entity != wi::ecs::INVALID_ENTITY && scene.objects.GetCount() == objects_before + 1,
            "reloaded mesh built")) {
        return false;
    }
    scene.Entity_Remove(entity);
    if (!check(scene.objects.GetCount() == objects_before, "reloaded mesh released")) {
        return false;
    }
    std::fprintf(stdout, "reload: positions=%u indices=%u objects_baseline=%u\n",
        (unsigned)reloaded.position_data.size(), (unsigned)reloaded.index_data.size(),
        (unsigned)objects_before);
    return true;
}

} // namespace probe
