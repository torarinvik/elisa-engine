#pragma once

namespace probe {

inline bool frame_completion_is_sound(wi::graphics::GraphicsDevice* device) {
    if (device == nullptr || !device->SupportsFrameCompletionQuery()) return false;
    const uint64_t submitted = device->GetFrameCount();
    if (device->IsFrameComplete(submitted + 1)) return false;
    device->WaitForGPU();
    return device->IsFrameComplete(submitted);
}

} // namespace probe
