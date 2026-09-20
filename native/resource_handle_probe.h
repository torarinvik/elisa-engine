#pragma once

#include "probe_core.h"
#include "probe_support.h"
#include "resource_handles.h"
#include "voice_handles.h"

#include <cstdio>

namespace probe {

inline bool probe_native_resource_handles(wi::scene::Scene& scene) {
    NativeResourceRegistry registry(scene);
    const NativeResourceHandle mesh = registry.create_cube("elisa_handle_mesh");
    const NativeResourceHandle material = registry.create_material("elisa_handle_material");
    const NativeResourceHandle body = registry.create_body("elisa_handle_body");
    const NativeResourceHandle texture = registry.create_texture();
    if (!check(registry.is_live(mesh) && mesh.kind == NativeResourceKind::Mesh,
            "mesh handle creation") ||
        !check(registry.is_live(material) && material.kind == NativeResourceKind::Material,
            "material handle creation") ||
        !check(registry.is_live(body) && scene.rigidbodies.GetComponent(registry.resolve(body)) != nullptr,
            "body handle creation") ||
        !check(registry.is_live(texture) && registry.resolve_texture(texture) != nullptr,
            "texture handle creation")) {
        return false;
    }
    if (!check(registry.set_material_color(material, XMFLOAT4(0.2f, 0.8f, 0.4f, 1.0f)) &&
        registry.set_visible(mesh, false) && registry.set_layer(mesh, 0x4u) &&
        registry.set_visible(mesh, true), "render resource instance updates")) return false;
    const NativeInstanceUpdate batched[] = {
        NativeInstanceUpdate{mesh, XMFLOAT3(2.0f, 0.0f, 1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f), 0x2u, true},
        NativeInstanceUpdate{body, XMFLOAT3(-2.0f, 0.0f, 1.0f), XMFLOAT3(0.5f, 0.5f, 0.5f), 0x4u, false},
    };
    if (!check(registry.apply_instance_batch(batched, 2), "render resource batch updates") ||
        !check(registry.apply_instance_batch(nullptr, 0), "empty render resource batch") ) return false;
    const NativeInstanceUpdate duplicate[] = {
        NativeInstanceUpdate{mesh, XMFLOAT3(3.0f, 0.0f, 1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f), 0x2u, true},
        NativeInstanceUpdate{mesh, XMFLOAT3(4.0f, 0.0f, 1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f), 0x2u, true},
    };
    if (!check(!registry.apply_instance_batch(duplicate, 2),
            "render resource batch rejects duplicate handles")) return false;
    const wi::ecs::Entity mesh_entity = registry.resolve(mesh);
    if (!check(mesh_entity != wi::ecs::INVALID_ENTITY, "mesh handle resolution") ||
        !check(registry.destroy(mesh), "mesh handle destruction") ||
        !check(!registry.is_live(mesh), "stale mesh handle rejection") ||
        !check(!registry.destroy(mesh), "double mesh destruction rejection")) {
        return false;
    }
    if (!check(registry.destroy_deferred(texture, 7), "texture logical destruction") ||
        !check(!registry.is_live(texture) && registry.pending_retirements() == 1,
            "texture retirement queued") ||
        !check(registry.resolve_texture(texture) == nullptr, "stale texture rejection")) {
        return false;
    }
    registry.collect_retired(6);
    if (!check(registry.pending_retirements() == 1, "texture waits for incomplete submission")) {
        return false;
    }
    registry.collect_retired(7);
    if (!check(registry.pending_retirements() == 0, "texture retirement collected") ||
        !check(registry.destroy(material), "material handle destruction") ||
        !check(registry.destroy(body), "body handle destruction")) {
        return false;
    }
    const NativeResourceHandle reclaimed = registry.create_cube("elisa_handle_reclaimed");
    if (!check(reclaimed.kind == NativeResourceKind::Mesh && reclaimed.generation != mesh.generation,
            "mesh generation reuse") ||
        !check(registry.destroy(reclaimed), "reclaimed mesh destruction")) {
        return false;
    }
    if (!check(registry.force_generation_for_test(NativeResourceKind::Material, material.slot, UINT32_MAX),
            "material generation exhaustion setup") ||
        !check(registry.create_material("elisa_generation_exhausted").slot == NativeResourceHandle::INVALID_SLOT,
            "material generation exhaustion rejection")) {
        return false;
    }
    const size_t objects_before_pressure = scene.objects.GetCount();
    for (int round = 0; round < 4; ++round) {
        std::vector<NativeResourceHandle> pressure;
        pressure.reserve(16);
        for (int index = 0; index < 16; ++index) {
            pressure.push_back(registry.create_cube("elisa_handle_pressure"));
        }
        for (const NativeResourceHandle handle : pressure) {
            if (!check(registry.is_live(handle) && registry.destroy(handle),
                       "resource pressure destroy")) {
                return false;
            }
        }
        if (!check(scene.objects.GetCount() == objects_before_pressure,
                   "resource pressure returns object baseline")) {
            return false;
        }
    }
    const auto telemetry = registry.telemetry();
    if (!check(telemetry.creations >= 68 && telemetry.failed_creations >= 1 &&
        telemetry.logical_destructions >= 68 && telemetry.retirements_enqueued == 1 &&
        telemetry.retirements_collected == 1 && telemetry.peak_pending_retirements == 1 &&
        telemetry.batched_update_calls >= 3 && telemetry.batched_update_rows >= 4 &&
        telemetry.rejected_update_batches >= 1,
        "resource allocator telemetry")) return false;
    std::fprintf(stdout, "resource handle pressure: rounds=4 batch=16 baseline=%u created=%llu failed=%llu retired=%llu/%llu peak=%llu\n",
        (unsigned)objects_before_pressure, (unsigned long long)telemetry.creations,
        (unsigned long long)telemetry.failed_creations, (unsigned long long)telemetry.retirements_collected,
        (unsigned long long)telemetry.retirements_enqueued, (unsigned long long)telemetry.peak_pending_retirements);
    wi::scene::Scene other_scene;
    NativeResourceRegistry other_registry(other_scene);
    return check(!other_registry.is_live(material), "cross-scene resource rejection");
}

inline bool probe_native_voice_handles() {
    audio::Service service;
    if (!check(service.initialize_null(), "voice service initialization")) return false;
    // Keep the voice alive through the handle/fence assertions; the null
    // backend callback advances in real time while the native probe starts.
    const std::vector<uint8_t> wav = make_test_wav(64000, 8000);
    const audio::ClipHandle clip = service.decode_clip(wav.data(), wav.size(), 8000, 1);
    if (!check(clip.slot < audio::MAX_CLIPS, "voice clip creation")) return false;
    NativeVoiceRegistry registry(service);
    const NativeResourceHandle voice = registry.play(clip);
    if (!check(registry.is_live(voice) && voice.kind == NativeResourceKind::Voice,
            "voice handle creation")) return false;
    if (!check(registry.destroy_deferred(voice, 3), "voice logical destruction") ||
        !check(!registry.is_live(voice), "stale voice handle rejection")) return false;
    registry.collect_retired(2);
    registry.collect_retired(3);
    if (!check(!service.voice_live(audio::VoiceHandle{voice.slot, voice.generation}),
            "voice fence retirement")) return false;
    const NativeResourceHandle reclaimed = registry.play(clip);
    if (!check(reclaimed.generation != voice.generation, "voice generation reuse") ||
        !check(registry.destroy(reclaimed), "voice destruction")) return false;
    if (!check(registry.force_generation_for_test(reclaimed.slot, UINT32_MAX),
            "voice generation exhaustion setup") ||
        !check(registry.play(clip).slot == NativeResourceHandle::INVALID_SLOT,
            "voice generation exhaustion rejection")) return false;
    service.shutdown();
    return check(!registry.is_live(reclaimed), "voice shutdown invalidation");
}

} // namespace probe
