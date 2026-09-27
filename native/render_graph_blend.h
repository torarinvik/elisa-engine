#pragma once

#include "wiGraphics.h"
#include "wiImage.h"

namespace elisa::render_graph {

inline bool blend_color(wi::graphics::Texture& source,
    wi::graphics::Texture& destination, float opacity,
    wi::graphics::CommandList command_list) {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return false;
    device->RenderPassBegin(&destination, command_list, false);
    wi::image::Params image;
    image.enableFullScreen();
    image.blendFlag = wi::enums::BLENDMODE_ALPHA;
    image.opacity = opacity;
    wi::image::Draw(&source, image, command_list);
    device->RenderPassEnd(command_list);
    return true;
}

} // namespace elisa::render_graph
