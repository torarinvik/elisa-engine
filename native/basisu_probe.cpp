// Basis Universal KTX2 transcoding at the untrusted asset boundary: the cooked
// KTX2 container is parsed and transcoded to RGBA on the CPU, so the engine
// owns the selected texture format instead of depending on a host's loader.
#include "probe_core.h"

#include "basisu_transcoder.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: basisu-probe <texture.ktx2>\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!probe::check(input.good(), "KTX2 file readable")) {
        return 1;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    basist::basisu_transcoder_init();
    basist::ktx2_transcoder transcoder;
    if (!probe::check(transcoder.init(bytes.data(), (uint32_t)bytes.size()), "KTX2 container parses")) {
        return 1;
    }
    const uint32_t width = transcoder.get_width();
    const uint32_t height = transcoder.get_height();
    if (!probe::check(width == 4 && height == 4, "KTX2 is the cooked 4x4 texture")) {
        return 1;
    }
    if (!probe::check(transcoder.start_transcoding(), "KTX2 starts transcoding")) {
        return 1;
    }
    const uint32_t pixel_count = width * height;
    std::vector<uint8_t> rgba((size_t)pixel_count * 4);
    const bool transcoded = transcoder.transcode_image_level(
        0, 0, 0, rgba.data(), pixel_count, basist::transcoder_texture_format::cTFRGBA32,
        0, width, height);
    if (!probe::check(transcoded, "KTX2 level transcodes to RGBA")) {
        return 1;
    }
    const bool green = rgba[1] > 200 and rgba[0] < 80 and rgba[2] < 120;
    if (!probe::check(green, "transcoded pixels are the cooked green")) {
        return 1;
    }
    std::fprintf(stdout, "basisu transcode: %ux%u uastc=%d rgba_bytes=%u first=%u,%u,%u\n",
        width, height, transcoder.is_uastc() ? 1 : 0, (unsigned)rgba.size(),
        (unsigned)rgba[0], (unsigned)rgba[1], (unsigned)rgba[2]);
    return 0;
}
