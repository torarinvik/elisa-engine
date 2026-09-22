#pragma once

#include "probe_core.h"
#include "wiScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace probe {

struct NativeAnimationHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
    uintptr_t owner = 0;
};

class AnimationSubmissionBridge {
public:
    static constexpr uint32_t MAX_INSTANCES = 16;
    static constexpr uint32_t MAX_BONES = 64;
    static constexpr uint32_t MAX_MORPHS = 32;
    static constexpr uint32_t MAX_MESHES = 256;

    explicit AnimationSubmissionBridge(wi::scene::Scene& scene)
        : scene_(scene), owner_(reinterpret_cast<uintptr_t>(this)) {}
    AnimationSubmissionBridge(const AnimationSubmissionBridge&) = delete;

    NativeAnimationHandle create(wi::ecs::Entity armature, wi::ecs::Entity mesh) {
        return create(armature, &mesh, 1);
    }

    NativeAnimationHandle create(wi::ecs::Entity armature, const wi::ecs::Entity* meshes, uint32_t mesh_count) {
        auto* skeleton = scene_.armatures.GetComponent(armature);
        if (skeleton == nullptr || skeleton->boneCollection.empty() || skeleton->boneCollection.size() > MAX_BONES ||
            meshes == nullptr || mesh_count == 0 || mesh_count > MAX_MESHES) {
            return {};
        }
        const wi::scene::MeshComponent* first = scene_.meshes.GetComponent(meshes[0]);
        if (first == nullptr || first->armatureID != armature || first->morph_targets.size() > MAX_MORPHS) return {};
        const uint32_t morph_count = uint32_t(first->morph_targets.size());
        for (uint32_t index = 0; index < mesh_count; ++index) {
            const wi::scene::MeshComponent* target = scene_.meshes.GetComponent(meshes[index]);
            if (target == nullptr || target->armatureID != armature || target->morph_targets.size() != morph_count) return {};
            for (uint32_t previous = 0; previous < index; ++previous)
                if (meshes[previous] == meshes[index]) return {};
        }
        const uint32_t slot = free_slot();
        if (slot == MAX_INSTANCES) return {};
        Entry& entry = entries_[slot];
        if (entry.generation == UINT32_MAX) return {};
        ++entry.generation;
        entry.armature = armature;
        entry.mesh_count = mesh_count;
        std::copy(meshes, meshes + mesh_count, entry.meshes.begin());
        entry.bone_count = uint32_t(skeleton->boneCollection.size());
        entry.morph_count = morph_count;
        entry.active = 0;
        entry.pending = false;
        entry.live = true;
        return {slot, entry.generation, owner_};
    }

    bool submit(NativeAnimationHandle handle, const XMFLOAT4X4* bones, uint32_t bone_count,
        const float* morphs, uint32_t morph_count) {
        Entry* entry = get(handle);
        if (entry == nullptr || entry->pending || bone_count != entry->bone_count || morph_count != entry->morph_count ||
            bones == nullptr || !finite_matrices(bones, bone_count) ||
            (morph_count > 0 && (morphs == nullptr || !finite_morphs(morphs, morph_count)))) return false;
        auto* skeleton = scene_.armatures.GetComponent(entry->armature);
        if (skeleton == nullptr || skeleton->boneCollection.size() != bone_count) return false;
        std::array<wi::scene::TransformComponent*, MAX_BONES> transforms{};
        for (uint32_t index = 0; index < bone_count; ++index) {
            transforms[index] = scene_.transforms.GetComponent(skeleton->boneCollection[index]);
            if (transforms[index] == nullptr) return false;
        }
        std::array<wi::scene::MeshComponent*, MAX_MESHES> meshes{};
        for (uint32_t mesh_index = 0; mesh_index < entry->mesh_count; ++mesh_index) {
            meshes[mesh_index] = scene_.meshes.GetComponent(entry->meshes[mesh_index]);
            if (meshes[mesh_index] == nullptr || meshes[mesh_index]->armatureID != entry->armature ||
                meshes[mesh_index]->morph_targets.size() != morph_count) return false;
        }
        const uint32_t next = 1u - entry->active;
        Buffer& buffer = entry->buffers[next];
        std::memcpy(buffer.bones.data(), bones, sizeof(XMFLOAT4X4) * bone_count);
        if (morph_count > 0) std::memcpy(buffer.morphs.data(), morphs, sizeof(float) * morph_count);
        for (uint32_t index = 0; index < bone_count; ++index) {
            transforms[index]->MatrixTransform(buffer.bones[index]);
        }
        for (uint32_t mesh_index = 0; mesh_index < entry->mesh_count; ++mesh_index)
            for (uint32_t index = 0; index < morph_count; ++index)
                meshes[mesh_index]->morph_targets[index].weight = buffer.morphs[index];
        entry->active = next;
        entry->pending = true;
        return true;
    }

    bool complete(NativeAnimationHandle handle) {
        Entry* entry = get(handle);
        if (entry == nullptr || !entry->pending) return false;
        entry->pending = false;
        return true;
    }

    bool pending(NativeAnimationHandle handle) const {
        const Entry* entry = get(handle);
        return entry != nullptr && entry->pending;
    }

    bool destroy(NativeAnimationHandle handle) {
        Entry* entry = get(handle);
        if (entry == nullptr || entry->pending) return false;
        entry->live = false;
        return true;
    }

private:
    struct Buffer {
        std::array<XMFLOAT4X4, MAX_BONES> bones{};
        std::array<float, MAX_MORPHS> morphs{};
    };
    struct Entry {
        wi::ecs::Entity armature = wi::ecs::INVALID_ENTITY;
        std::array<wi::ecs::Entity, MAX_MESHES> meshes{};
        std::array<Buffer, 2> buffers{};
        uint32_t generation = 0;
        uint32_t bone_count = 0;
        uint32_t morph_count = 0;
        uint32_t mesh_count = 0;
        uint32_t active = 0;
        bool pending = false;
        bool live = false;
    };

    static bool finite_matrices(const XMFLOAT4X4* matrices, uint32_t count) {
        for (uint32_t index = 0; index < count; ++index) {
            const float* values = &matrices[index].m[0][0];
            for (uint32_t component = 0; component < 16; ++component)
                if (!std::isfinite(values[component])) return false;
        }
        return true;
    }
    static bool finite_morphs(const float* morphs, uint32_t count) {
        for (uint32_t index = 0; index < count; ++index)
            if (!std::isfinite(morphs[index]) || morphs[index] < 0.0f || morphs[index] > 1.0f) return false;
        return true;
    }
    Entry* get(NativeAnimationHandle handle) {
        return handle.owner == owner_ && handle.slot < MAX_INSTANCES && entries_[handle.slot].live &&
            entries_[handle.slot].generation == handle.generation ? &entries_[handle.slot] : nullptr;
    }
    const Entry* get(NativeAnimationHandle handle) const {
        return handle.owner == owner_ && handle.slot < MAX_INSTANCES && entries_[handle.slot].live &&
            entries_[handle.slot].generation == handle.generation ? &entries_[handle.slot] : nullptr;
    }
    uint32_t free_slot() const {
        for (uint32_t index = 0; index < MAX_INSTANCES; ++index) if (!entries_[index].live) return index;
        return MAX_INSTANCES;
    }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    std::array<Entry, MAX_INSTANCES> entries_{};
};

inline bool probe_animation_submission(wi::scene::Scene& scene) {
    const auto armature = scene.Entity_CreateTransform("elisa_armature");
    const auto bone = scene.Entity_CreateTransform("elisa_bone");
    const auto mesh = scene.Entity_CreateCube("elisa_animated_mesh");
    scene.Component_Attach(bone, armature, true);
    auto& skeleton = scene.armatures.Create(armature);
    auto* target = scene.meshes.GetComponent(mesh);
    if (target == nullptr) return false;
    skeleton.boneCollection.push_back(bone);
    skeleton.inverseBindMatrices.push_back(wi::math::IDENTITY_MATRIX);
    target->armatureID = armature;
    target->morph_targets.resize(1);
    AnimationSubmissionBridge bridge(scene);
    const auto first = bridge.create(armature, mesh);
    XMFLOAT4X4 pose = wi::math::IDENTITY_MATRIX;
    pose._41 = 2.0f;
    const float weight = 0.75f;
    if (!check(bridge.submit(first, &pose, 1, &weight, 1) && bridge.pending(first),
        "animation submits an owned pose") ||
        !check(scene.transforms.GetComponent(bone)->translation_local.x == 2.0f &&
            target->morph_targets[0].weight == weight, "animation applies bone and morph state") ||
        !check(!bridge.submit(first, &pose, 1, &weight, 1) || bridge.pending(first),
            "animation keeps the in-flight buffer alive")) return false;
    const auto foreign = AnimationSubmissionBridge(scene).create(armature, mesh);
    if (!check(!bridge.complete(foreign) && bridge.complete(first) && bridge.destroy(first),
        "animation completes and retires owned pose")) return false;
    scene.Entity_Remove(mesh);
    scene.Entity_Remove(bone);
    scene.Entity_Remove(armature);
    return check(scene.armatures.GetComponent(armature) == nullptr, "animation resources unload");
}

} // namespace probe
