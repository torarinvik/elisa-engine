#pragma once

// Synchronous Wicked adapter. Completed staging buffers can instead use the
// CPU-only rgba_png.h encoder directly, without issuing another GPU readback.
#include "wiHelper.h"
#include "wiGraphics.h"
#include "rgba_png.h"

namespace probe {

inline bool save_rgba_png(const wi::graphics::Texture& texture, const std::string& path) {
    if (!texture.IsValid()) return false;
    const auto desc = texture.GetDesc();
    if (desc.width == 0 || desc.height == 0 ||
        desc.width > elisa::capture::MAX_RGBA_BYTES / 4 ||
        desc.height > elisa::capture::MAX_RGBA_BYTES / (size_t(desc.width) * 4)) return false;
    elisa::capture::PixelOrder order;
    switch (desc.format) {
    case wi::graphics::Format::R8G8B8A8_UNORM:
    case wi::graphics::Format::R8G8B8A8_UNORM_SRGB:
        order = elisa::capture::PixelOrder::RGBA;
        break;
    case wi::graphics::Format::B8G8R8A8_UNORM:
    case wi::graphics::Format::B8G8R8A8_UNORM_SRGB:
        order = elisa::capture::PixelOrder::BGRA;
        break;
    case wi::graphics::Format::R10G10B10A2_UNORM:
#ifdef __APPLE__
        order = elisa::capture::PixelOrder::BGR10A2;
#else
        order = elisa::capture::PixelOrder::RGB10A2;
#endif
        break;
    default:
        return false;
    }
    if (desc.array_size != 1 || desc.mip_levels != 1 || desc.depth != 1) return false;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return false;
    wi::graphics::TextureDesc staging_desc = desc;
    staging_desc.usage = wi::graphics::Usage::READBACK;
    staging_desc.layout = wi::graphics::ResourceState::COPY_DST;
    staging_desc.bind_flags = wi::graphics::BindFlag::NONE;
    staging_desc.misc_flags = wi::graphics::ResourceMiscFlag::NONE;
    wi::graphics::Texture staging;
    if (!device->CreateTexture(&staging_desc, nullptr, &staging)) return false;
    const wi::graphics::CommandList command = device->BeginCommandList(wi::graphics::QUEUE_GRAPHICS);
    const wi::graphics::GPUBarrier to_copy = wi::graphics::GPUBarrier::Image(
        &texture, desc.layout, wi::graphics::ResourceState::COPY_SRC);
    device->Barrier(&to_copy, 1, command);
    device->CopyResource(&staging, &texture, command);
    const wi::graphics::GPUBarrier restore = wi::graphics::GPUBarrier::Image(
        &texture, wi::graphics::ResourceState::COPY_SRC, desc.layout);
    device->Barrier(&restore, 1, command);
    device->SubmitCommandLists();
    device->WaitForGPU();
    if (staging.mapped_subresources == nullptr || staging.mapped_subresource_count == 0) return false;
    const wi::graphics::SubresourceData& mapped = staging.mapped_subresources[0];
    const size_t byte_count = mapped.slice_pitch != 0 ? mapped.slice_pitch : staging.mapped_size;
    return elisa::capture::save_rgba_png(static_cast<const uint8_t*>(mapped.data_ptr),
        byte_count, desc.width, desc.height, mapped.row_pitch, order, path);
}

} // namespace probe
