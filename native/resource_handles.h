#pragma once

// Typed native handles for resources owned by one Wicked scene. Handles carry
// only a kind, slot, generation, and owner identity; vendor pointers never
// cross this boundary. Logical destruction is immediate, while resources
// tagged with a submission serial stay alive until the caller collects them.
#include "wiGraphicsDevice.h"
#include "wiScene.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace probe {

enum class NativeResourceKind : uint8_t { Mesh, Material, Texture, Body, Voice, Count, Invalid = 255 };

struct NativeResourceHandle {
    static constexpr uint32_t INVALID_SLOT = UINT32_MAX;
    uint32_t slot = INVALID_SLOT;
    uint32_t generation = 0;
    uintptr_t owner = 0;
    NativeResourceKind kind = NativeResourceKind::Invalid;
};

class NativeResourceRegistry {
public:
    static constexpr uint32_t MAX_RESOURCES = 64;

    struct Telemetry {
        uint64_t creations = 0;
        uint64_t failed_creations = 0;
        uint64_t logical_destructions = 0;
        uint64_t retirements_enqueued = 0;
        uint64_t retirements_collected = 0;
        uint64_t peak_pending_retirements = 0;
    };

    explicit NativeResourceRegistry(wi::scene::Scene& scene)
        : scene_(scene), owner_(reinterpret_cast<uintptr_t>(&scene)) {}

    NativeResourceRegistry(const NativeResourceRegistry&) = delete;
    NativeResourceRegistry& operator=(const NativeResourceRegistry&) = delete;

    NativeResourceHandle create_cube(const std::string& name) {
        return create_entity(NativeResourceKind::Mesh, [&] { return scene_.Entity_CreateCube(name); });
    }

    NativeResourceHandle create_mesh(const std::string& name) {
        return create_entity(NativeResourceKind::Mesh, [&] { return scene_.Entity_CreateMesh(name); });
    }

    NativeResourceHandle create_material(const std::string& name) {
        return create_entity(NativeResourceKind::Material, [&] { return scene_.Entity_CreateMaterial(name); });
    }

    NativeResourceHandle create_body(const std::string& name) {
        return create_entity(NativeResourceKind::Body, [&] {
            const wi::ecs::Entity entity = scene_.Entity_CreateCube(name);
            if (entity != wi::ecs::INVALID_ENTITY) {
                auto& body = scene_.rigidbodies.Create(entity);
                body.shape = wi::scene::RigidBodyPhysicsComponent::BOX;
                body.mass = 1.0f;
                body.box.halfextents = XMFLOAT3(0.5f, 0.5f, 0.5f);
            }
            return entity;
        });
    }

    NativeResourceHandle create_texture() {
        const uint32_t slot = free_slot(NativeResourceKind::Texture);
        if (slot == MAX_RESOURCES || wi::graphics::GetDevice() == nullptr) {
            ++telemetry_.failed_creations;
            return {};
        }
        Slot& state = slots_[kind_index(NativeResourceKind::Texture)][slot];
        wi::graphics::TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.depth = 1;
        desc.array_size = 1;
        desc.mip_levels = 1;
        desc.sample_count = 1;
        desc.format = wi::graphics::Format::R8G8B8A8_UNORM;
        desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE;
        const uint32_t pixel = 0xFFFFFFFFu;
        wi::graphics::SubresourceData data;
        data.data_ptr = &pixel;
        data.row_pitch = sizeof(pixel);
        data.slice_pitch = sizeof(pixel);
        if (!wi::graphics::GetDevice()->CreateTexture(&desc, &data, &state.texture) ||
            !state.texture.IsValid() || !next_generation(state)) {
            state.texture = {};
            ++telemetry_.failed_creations;
            return {};
        }
        state.live = true;
        ++telemetry_.creations;
        return make_handle(NativeResourceKind::Texture, slot, state);
    }

    bool is_live(NativeResourceHandle handle) const {
        const Slot* state = state_for(handle);
        return state != nullptr && state->live && state->generation == handle.generation;
    }

    wi::ecs::Entity resolve(NativeResourceHandle handle) const {
        const Slot* state = state_for(handle);
        return is_entity_kind(handle.kind) && is_live(handle) ? state->entity : wi::ecs::INVALID_ENTITY;
    }

    const wi::graphics::Texture* resolve_texture(NativeResourceHandle handle) const {
        const Slot* state = state_for(handle);
        return handle.kind == NativeResourceKind::Texture && is_live(handle) ? &state->texture : nullptr;
    }

    bool set_material_color(NativeResourceHandle handle, XMFLOAT4 color) {
        if (handle.kind != NativeResourceKind::Material || !is_live(handle)) return false;
        auto* material = scene_.materials.GetComponent(resolve(handle));
        if (material == nullptr) return false;
        material->baseColor = color;
        material->SetDirty();
        return true;
    }

    bool set_visible(NativeResourceHandle handle, bool visible) {
        if (!is_entity_kind(handle.kind) || !is_live(handle)) return false;
        auto* object = scene_.objects.GetComponent(resolve(handle));
        if (object == nullptr) return false;
        object->SetRenderable(visible);
        return true;
    }

    bool set_layer(NativeResourceHandle handle, uint32_t layer_mask) {
        if (!is_entity_kind(handle.kind) || !is_live(handle)) return false;
        auto* object = scene_.objects.GetComponent(resolve(handle));
        if (object == nullptr) return false;
        object->filterMask = layer_mask;
        return true;
    }

    bool destroy(NativeResourceHandle handle) {
        Slot* state = state_for(handle);
        if (state == nullptr || !is_live(handle)) return false;
        release_now(handle.kind, *state);
        ++telemetry_.logical_destructions;
        return true;
    }

    bool destroy_deferred(NativeResourceHandle handle, uint64_t submission_serial = 0) {
        Slot* state = state_for(handle);
        if (state == nullptr || !is_live(handle)) return false;
        retired_.push_back({handle.kind, state->entity, state->texture, handle.slot, submission_serial});
        state->live = false;
        state->retired = true;
        ++telemetry_.logical_destructions;
        ++telemetry_.retirements_enqueued;
        if (retired_.size() > telemetry_.peak_pending_retirements) {
            telemetry_.peak_pending_retirements = retired_.size();
        }
        return true;
    }

    size_t pending_retirements() const { return retired_.size(); }
    const Telemetry& telemetry() const { return telemetry_; }

    void collect_retired(uint64_t completed_serial = UINT64_MAX) {
        size_t write = 0;
        for (const Retired& resource : retired_) {
            if (resource.serial > completed_serial) {
                retired_[write++] = resource;
                continue;
            }
            if (is_entity_kind(resource.kind) && resource.entity != wi::ecs::INVALID_ENTITY) {
                scene_.Entity_Remove(resource.entity);
            }
            Slot& state = slots_[kind_index(resource.kind)][resource.slot];
            state.entity = wi::ecs::INVALID_ENTITY;
            state.texture = {};
            state.retired = false;
            ++telemetry_.retirements_collected;
        }
        retired_.resize(write);
    }

    // Test-only fault injection makes generation exhaustion deterministic
    // without exposing mutable generation state to the public engine ABI.
    bool force_generation_for_test(NativeResourceKind kind, uint32_t slot, uint32_t generation) {
        if (kind >= NativeResourceKind::Count || slot >= MAX_RESOURCES) return false;
        Slot& state = slots_[kind_index(kind)][slot];
        if (state.live || state.retired) return false;
        state.generation = generation;
        return true;
    }

private:
    struct Slot {
        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        wi::graphics::Texture texture;
        uint32_t generation = 0;
        bool live = false;
        bool retired = false;
    };

    struct Retired {
        NativeResourceKind kind = NativeResourceKind::Invalid;
        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        wi::graphics::Texture texture;
        uint32_t slot = NativeResourceHandle::INVALID_SLOT;
        uint64_t serial = 0;
    };

    static constexpr size_t kind_index(NativeResourceKind kind) {
        return static_cast<size_t>(kind);
    }

    static constexpr bool is_entity_kind(NativeResourceKind kind) {
        return kind != NativeResourceKind::Texture && kind != NativeResourceKind::Voice &&
            kind < NativeResourceKind::Count;
    }

    uint32_t free_slot(NativeResourceKind kind) const {
        if (kind >= NativeResourceKind::Count) return MAX_RESOURCES;
        const auto& pool = slots_[kind_index(kind)];
        for (uint32_t slot = 0; slot < MAX_RESOURCES; ++slot) {
            if (!pool[slot].live && !pool[slot].retired) return slot;
        }
        return MAX_RESOURCES;
    }

    Slot* state_for(NativeResourceHandle handle) {
        return handle.owner == owner_ && handle.kind < NativeResourceKind::Count &&
                handle.slot < MAX_RESOURCES ? &slots_[kind_index(handle.kind)][handle.slot] : nullptr;
    }

    const Slot* state_for(NativeResourceHandle handle) const {
        return handle.owner == owner_ && handle.kind < NativeResourceKind::Count &&
                handle.slot < MAX_RESOURCES ? &slots_[kind_index(handle.kind)][handle.slot] : nullptr;
    }

    bool next_generation(Slot& state) {
        if (state.generation == UINT32_MAX) return false;
        state.generation = state.generation == 0 ? 1 : state.generation + 1;
        return true;
    }

    NativeResourceHandle make_handle(NativeResourceKind kind, uint32_t slot, const Slot& state) const {
        return NativeResourceHandle{slot, state.generation, owner_, kind};
    }

    template <typename Factory>
    NativeResourceHandle create_entity(NativeResourceKind kind, Factory&& factory) {
        const uint32_t slot = free_slot(kind);
        if (slot == MAX_RESOURCES) {
            ++telemetry_.failed_creations;
            return {};
        }
        Slot& state = slots_[kind_index(kind)][slot];
        const wi::ecs::Entity entity = factory();
        if (entity == wi::ecs::INVALID_ENTITY || !next_generation(state)) {
            if (entity != wi::ecs::INVALID_ENTITY) scene_.Entity_Remove(entity);
            ++telemetry_.failed_creations;
            return {};
        }
        state.entity = entity;
        state.live = true;
        ++telemetry_.creations;
        return make_handle(kind, slot, state);
    }

    void release_now(NativeResourceKind kind, Slot& state) {
        if (is_entity_kind(kind) && state.entity != wi::ecs::INVALID_ENTITY) {
            scene_.Entity_Remove(state.entity);
        }
        state.entity = wi::ecs::INVALID_ENTITY;
        state.texture = {};
        state.live = false;
    }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    std::array<std::array<Slot, MAX_RESOURCES>, static_cast<size_t>(NativeResourceKind::Count)> slots_{};
    std::vector<Retired> retired_;
    Telemetry telemetry_;
};

} // namespace probe
