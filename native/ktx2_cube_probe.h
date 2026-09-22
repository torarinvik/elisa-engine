#pragma once

#include "ktx2_upload.h"

#include <filesystem>

namespace probe {

inline bool check_ktx2_cubemap_upload(const std::filesystem::path& path) {
    if (!check(std::filesystem::is_regular_file(path), "KTX2 cubemap artifact present")) return false;
    const wi::Resource resource = load_ktx2_texture_resource(path.lexically_normal().string());
    if (!check(resource.IsValid() && resource.GetTexture().IsValid(),
        "KTX2 cubemap uploaded to Wicked GPU")) return false;
    const wi::graphics::TextureDesc& desc = resource.GetTexture().GetDesc();
    return check(desc.array_size == 6 && desc.width == desc.height &&
        desc.misc_flags == wi::graphics::ResourceMiscFlag::TEXTURECUBE,
        "KTX2 upload preserves cubemap shape");
}

} // namespace probe
