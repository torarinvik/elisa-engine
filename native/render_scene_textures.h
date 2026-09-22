#pragma once

// Private Wicked texture assignment for the engine-owned render-scene ABI.
#include "cooked_geometry_package.h"
#include "Utility/stb_image.h"
#include "wiResourceManager.h"
#include "wiScene.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace elisa::rendering::textures {

enum class Slot : int32_t {
    BaseColor = 0,
    Normal = 1,
    Surface = 2,
    Emissive = 3,
    Occlusion = 4,
    Count = 5,
};

inline constexpr int32_t SLOT_COUNT = static_cast<int32_t>(Slot::Count);
inline constexpr std::array<uint32_t, SLOT_COUNT> MATERIAL_SLOTS = {
    wi::scene::MaterialComponent::BASECOLORMAP,
    wi::scene::MaterialComponent::NORMALMAP,
    wi::scene::MaterialComponent::SURFACEMAP,
    wi::scene::MaterialComponent::EMISSIVEMAP,
    wi::scene::MaterialComponent::OCCLUSIONMAP,
};

inline constexpr size_t MAX_DECODED_IMAGE_BYTES = 256u * 1024u * 1024u;
inline constexpr uint32_t MAX_TEXTURE_MIPS = 16;
inline constexpr int32_t GRAYSCALE_CHANNELS = 1;
inline constexpr int32_t GRAYSCALE_ALPHA_CHANNELS = 2;
inline constexpr int32_t RGB_CHANNELS = 3;
inline constexpr int32_t RGBA_CHANNELS = 4;
inline constexpr uint8_t OPAQUE_ALPHA = 255;

struct DecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t source_channels = 0;
    std::vector<uint8_t> pixels;
};

inline bool valid_slot(int32_t slot) {
    return slot >= 0 && slot < SLOT_COUNT;
}

// Decode bundle PNG/JPEG bytes without touching Wicked's graphics device.
// The caller may run this on an asset worker; GPU creation stays owner-thread-only.
inline bool decode_image(const std::vector<uint8_t>& encoded, uint32_t expected_width,
        uint32_t expected_height, DecodedImage& image) {
    image = DecodedImage{};
    if (encoded.empty() || encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
        stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 0),
        &stbi_image_free);
    if (decoded == nullptr || width <= 0 || height <= 0 ||
        channels < GRAYSCALE_CHANNELS || channels > RGBA_CHANNELS ||
        static_cast<uint32_t>(width) != expected_width || static_cast<uint32_t>(height) != expected_height) {
        return false;
    }
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t stored_channels = channels == RGB_CHANNELS
        ? RGBA_CHANNELS : static_cast<size_t>(channels);
    if (pixel_count > MAX_DECODED_IMAGE_BYTES / stored_channels) return false;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.source_channels = channels;
    if (channels != RGB_CHANNELS) {
        image.pixels.assign(decoded.get(), decoded.get() + pixel_count * stored_channels);
        return true;
    }
    image.pixels.resize(pixel_count * stored_channels);
    for (size_t index = 0; index < pixel_count; ++index) {
        image.pixels[index * RGBA_CHANNELS] = decoded.get()[index * RGB_CHANNELS];
        image.pixels[index * RGBA_CHANNELS + 1] = decoded.get()[index * RGB_CHANNELS + 1];
        image.pixels[index * RGBA_CHANNELS + 2] = decoded.get()[index * RGB_CHANNELS + 2];
        image.pixels[index * RGBA_CHANNELS + 3] = OPAQUE_ALPHA;
    }
    return true;
}

inline wi::resourcemanager::Flags import_flags(int32_t slot_code) {
    auto flags = wi::resourcemanager::Flags::IMPORT_BLOCK_COMPRESSED;
    if (slot_code == static_cast<int32_t>(Slot::Normal)) {
        flags |= wi::resourcemanager::Flags::IMPORT_NORMALMAP;
    }
    return flags;
}

inline bool assign_resource(wi::scene::MaterialComponent& material, int32_t slot_code,
        wi::Resource resource, std::string resource_name) {
    if (!resource.IsValid() || !resource.GetTexture().IsValid()) return false;
    auto& texture = material.textures[MATERIAL_SLOTS[static_cast<size_t>(slot_code)]];
    texture.resource = std::move(resource);
    texture.name.swap(resource_name);
    material.SetDirty();
    return true;
}

inline bool assign_resolved_texture(wi::scene::MaterialComponent& material,
        int32_t slot_code, const std::filesystem::path& resolved_path) {
    if (!valid_slot(slot_code)) return false;
    if (resolved_path.empty()) return false;
    wi::Resource resource = wi::resourcemanager::Load(resolved_path.string(), import_flags(slot_code));
    return assign_resource(material, slot_code, std::move(resource), resolved_path.string());
}

// Upload a worker-decoded bundle image on the owner thread. Keep one Wicked
// resource per material role because normal maps use BC5 while color roles use
// the source image's alpha/compression format.
inline wi::Resource create_decoded_resource(const std::string& resource_name, int32_t slot_code,
        const DecodedImage& image) {
    wi::Resource resource;
    if (!valid_slot(slot_code) || image.width == 0 || image.height == 0 || image.pixels.empty()) return resource;
    const size_t pixel_count = static_cast<size_t>(image.width) * image.height;
    const size_t source_channels = static_cast<size_t>(image.source_channels);
    const size_t stored_channels = image.source_channels == RGB_CHANNELS
        ? RGBA_CHANNELS : source_channels;
    if (source_channels < GRAYSCALE_CHANNELS || source_channels > RGBA_CHANNELS ||
        pixel_count > MAX_DECODED_IMAGE_BYTES / stored_channels ||
        image.pixels.size() != pixel_count * stored_channels) return resource;

    using namespace wi::graphics;
    GraphicsDevice* device = GetDevice();
    if (device == nullptr) return resource;

    Format format = Format::R8G8B8A8_UNORM;
    Format compressed_format = Format::BC3_UNORM;
    Swizzle swizzle = {ComponentSwizzle::R, ComponentSwizzle::G, ComponentSwizzle::B, ComponentSwizzle::A};
    switch (image.source_channels) {
    case GRAYSCALE_CHANNELS:
        format = Format::R8_UNORM;
        compressed_format = Format::BC4_UNORM;
        swizzle = {ComponentSwizzle::R, ComponentSwizzle::R, ComponentSwizzle::R, ComponentSwizzle::ONE};
        break;
    case GRAYSCALE_ALPHA_CHANNELS:
        format = Format::R8G8_UNORM;
        compressed_format = Format::BC5_UNORM;
        swizzle = {ComponentSwizzle::R, ComponentSwizzle::R, ComponentSwizzle::R, ComponentSwizzle::G};
        break;
    case RGB_CHANNELS:
        swizzle = {ComponentSwizzle::R, ComponentSwizzle::G, ComponentSwizzle::B, ComponentSwizzle::ONE};
        compressed_format = Format::BC1_UNORM;
        break;
    default:
        break;
    }

    TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.depth = 1;
    desc.array_size = 1;
    desc.sample_count = 1;
    desc.layout = ResourceState::SHADER_RESOURCE;
    desc.format = format;
    desc.swizzle = swizzle;
    desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
    desc.mip_levels = GetMipCount(desc.width, desc.height);
    if (desc.mip_levels > MAX_TEXTURE_MIPS) return resource;
    desc.usage = Usage::DEFAULT;
    desc.misc_flags = ResourceMiscFlag::TYPED_FORMAT_CASTING;

    std::array<SubresourceData, MAX_TEXTURE_MIPS> initial_data{};
    uint32_t mip_width = image.width;
    for (uint32_t mip = 0; mip < desc.mip_levels; ++mip) {
        initial_data[mip].data_ptr = image.pixels.data();
        initial_data[mip].row_pitch = mip_width * GetFormatStride(desc.format);
        mip_width = std::max(1u, mip_width / 2);
    }
    Texture uncompressed;
    if (!device->CreateTexture(&desc, initial_data.data(), &uncompressed)) return resource;
    device->SetName(&uncompressed, resource_name.c_str());
    device->CreateMipgenSubresources(uncompressed);

    int srgb_subresource = -1;
    const Format srgb_format = GetFormatSRGB(desc.format);
    if (srgb_format != Format::UNKNOWN && srgb_format != desc.format) {
        srgb_subresource = device->CreateSubresource(&uncompressed, SubresourceType::SRV,
            0, -1, 0, -1, &srgb_format);
    }
    wi::renderer::AddDeferredMIPGen(uncompressed, true);

    Texture texture;
    if (slot_code == static_cast<int32_t>(Slot::Normal)) {
        compressed_format = Format::BC5_UNORM;
        swizzle = {ComponentSwizzle::R, ComponentSwizzle::G,
            ComponentSwizzle::ONE, ComponentSwizzle::ONE};
    }
    Texture compression_source = std::move(uncompressed);
    srgb_subresource = -1;
    desc.format = compressed_format;
    desc.swizzle = swizzle;
    desc.bind_flags = BindFlag::SHADER_RESOURCE;
    const uint32_t block_size = GetFormatBlockSize(desc.format);
    desc.width = align(desc.width, block_size);
    desc.height = align(desc.height, block_size);
    desc.mip_levels = GetMipCount(desc.width, desc.height, 1, block_size);
    if (!device->CreateTexture(&desc, nullptr, &texture)) return resource;
    device->SetName(&texture, resource_name.c_str());
    const Format compressed_srgb_format = GetFormatSRGB(desc.format);
    if (compressed_srgb_format != Format::UNKNOWN && compressed_srgb_format != desc.format) {
        srgb_subresource = device->CreateSubresource(&texture, SubresourceType::SRV,
            0, -1, 0, -1, &compressed_srgb_format);
    }
    wi::renderer::AddDeferredBlockCompression(compression_source, texture);
    resource.SetTexture(texture, srgb_subresource);
    return resource;
}

inline bool assign_decoded_texture(wi::scene::MaterialComponent& material, int32_t slot_code,
        wi::Resource& cached_resource, const std::string& resource_stem, const char* extension,
        const DecodedImage& image) {
    if (!valid_slot(slot_code) || resource_stem.empty() || extension == nullptr) return false;
    std::string resource_name = resource_stem;
    if (slot_code == static_cast<int32_t>(Slot::Normal)) resource_name += ".normal";
    if (slot_code == static_cast<int32_t>(Slot::Occlusion)) resource_name += ".occlusion";
    resource_name += ".";
    resource_name += extension;
    if (!cached_resource.IsValid()) {
        cached_resource = create_decoded_resource(resource_name, slot_code, image);
    }
    // A nonempty MaterialComponent texture name makes CreateRenderData reload
    // it through Wicked's synchronous resource manager, undoing worker decode.
    return assign_resource(material, slot_code, cached_resource, {});
}

inline bool assign_project_texture(wi::scene::MaterialComponent& material,
        int32_t slot_code, const char* asset_path) {
    std::filesystem::path resolved_path;
    if (!elisa::assets::resolve_project_asset_path(asset_path, resolved_path)) return false;
    return assign_resolved_texture(material, slot_code, resolved_path);
}

} // namespace elisa::rendering::textures
