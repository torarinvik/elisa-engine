#pragma once

#include "probe_core.h"
#include "wiRenderPath3D.h"
#include "wiScene.h"

#include <array>
#include <cstdint>

namespace probe {

class SelectionOutlineBridge {
public:
    static constexpr uint32_t MAX_MATERIALS = 16;

    SelectionOutlineBridge(wi::scene::Scene& scene, wi::RenderPath3D& path)
        : scene_(scene), path_(path) {}
    SelectionOutlineBridge(const SelectionOutlineBridge&) = delete;

    bool select(wi::ecs::Entity object) {
        clear();
        auto* object_component = scene_.objects.GetComponent(object);
        auto* mesh = object_component == nullptr ? nullptr : scene_.meshes.GetComponent(object_component->meshID);
        if (mesh == nullptr || mesh->subsets.size() > MAX_MATERIALS) return false;
        for (const auto& subset : mesh->subsets) {
            auto* material = scene_.materials.GetComponent(subset.materialID);
            if (material == nullptr) {
                clear();
                return false;
            }
            material->SetOutlineEnabled(true);
            materials_[count_++] = subset.materialID;
        }
        path_.setOutlineEnabled(count_ > 0);
        selected_ = object;
        return count_ > 0;
    }

    void clear() {
        for (uint32_t index = 0; index < count_; ++index) {
            auto* material = scene_.materials.GetComponent(materials_[index]);
            if (material != nullptr) material->SetOutlineEnabled(false);
        }
        count_ = 0;
        selected_ = wi::ecs::INVALID_ENTITY;
        path_.setOutlineEnabled(false);
    }

    bool selected(wi::ecs::Entity object) const { return selected_ == object; }

private:
    wi::scene::Scene& scene_;
    wi::RenderPath3D& path_;
    std::array<wi::ecs::Entity, MAX_MATERIALS> materials_{};
    wi::ecs::Entity selected_ = wi::ecs::INVALID_ENTITY;
    uint32_t count_ = 0;
};

inline bool probe_selection_outline(wi::scene::Scene& scene, wi::RenderPath3D& path) {
    const auto target = scene.Entity_CreateCube("elisa_outline_target");
    auto* object = scene.objects.GetComponent(target);
    auto* mesh = object == nullptr ? nullptr : scene.meshes.GetComponent(object->meshID);
    if (!check(object != nullptr && mesh != nullptr && !mesh->subsets.empty(),
        "outline target has a material subset")) return false;
    SelectionOutlineBridge bridge(scene, path);
    auto* material = scene.materials.GetComponent(mesh->subsets.front().materialID);
    if (!check(bridge.select(target) && bridge.selected(target) && path.getOutlineEnabled() &&
        material != nullptr && material->IsOutlineEnabled(), "outline selects native material")) return false;
    bridge.clear();
    if (!check(!bridge.selected(target) && !path.getOutlineEnabled() && !material->IsOutlineEnabled(),
        "outline clears native selection")) return false;
    scene.Entity_Remove(target);
    return check(scene.objects.GetComponent(target) == nullptr, "outline target unloads");
}

} // namespace probe
