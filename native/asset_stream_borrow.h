#pragma once

// Render-side access to resident application asset-stream requests. Both
// calls run on the application owner thread and take the stream lock only for
// the copy, so callers must not hold the asset stream mutex.
#include "wiGraphics.h"

#include <cstdint>
#include <vector>

// Copies the decoded bytes of a resident request. False for any other stage.
bool elisa_asset_stream_copy_bytes(int64_t handle, std::vector<uint8_t>& bytes);

// Shares the GPU texture the loader uploaded for a resident request; the copy
// keeps the GPU resource alive for as long as the caller holds it.
bool elisa_asset_stream_share_texture(int64_t handle, wi::graphics::Texture& texture);
