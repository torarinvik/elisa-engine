// CPU reference validation for cooked Basis KTX2 textures; runtime format
// selection and GPU upload are exercised by the Wicked native probe.
#include "probe_core.h"

#include "basisu_transcoder.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::fprintf(stderr, "usage: basisu-probe <texture.ktx2> [cubemap.ktx2 [alpha.ktx2]]\n");
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
    uint8_t bc1_block[8] = {};
    if (!probe::check(transcoder.transcode_image_level(
        0, 0, 0, bc1_block, 1, basist::transcoder_texture_format::cTFBC1_RGB, 0, 1),
        "opaque KTX2 transcodes to one BC1 block")) return 1;
    std::fprintf(stdout, "basisu transcode: %ux%u uastc=%d rgba_bytes=%u first=%u,%u,%u\n",
        width, height, transcoder.is_uastc() ? 1 : 0, (unsigned)rgba.size(),
        (unsigned)rgba[0], (unsigned)rgba[1], (unsigned)rgba[2]);
    if (argc >= 3) {
        std::ifstream cube_input(argv[2], std::ios::binary);
        if (!probe::check(cube_input.good(), "KTX2 cubemap file readable")) return 1;
        const std::vector<uint8_t> cube_bytes(
            (std::istreambuf_iterator<char>(cube_input)), std::istreambuf_iterator<char>());
        basist::ktx2_transcoder cube;
        if (!probe::check(cube.init(cube_bytes.data(), (uint32_t)cube_bytes.size()),
            "KTX2 cubemap parses")) return 1;
        if (!probe::check(cube.get_width() == 4 && cube.get_height() == 4 &&
            cube.get_layers() <= 1 && cube.get_faces() == 6, "KTX2 has six square cube faces")) return 1;
        if (!probe::check(cube.start_transcoding(), "KTX2 cubemap starts transcoding")) return 1;
        const uint8_t expected[6][3] = {
            {245, 35, 35}, {30, 240, 45}, {35, 50, 245},
            {240, 230, 25}, {235, 35, 230}, {25, 225, 225},
        };
        for (uint32_t face = 0; face < 6; ++face) {
            basist::ktx2_image_level_info face_info{};
            if (!probe::check(cube.get_image_level_info(face_info, 0, 0, face) &&
                face_info.m_orig_width == 4 && face_info.m_orig_height == 4,
                "KTX2 cubemap face metadata")) return 1;
            std::vector<uint8_t> face_rgba(4 * 4 * 4);
            if (!probe::check(cube.transcode_image_level(
                0, 0, face, face_rgba.data(), 4 * 4,
                basist::transcoder_texture_format::cTFRGBA32, 0, 4, 4),
                "KTX2 cubemap face transcodes")) return 1;
            const uint8_t* pixel = face_rgba.data();
            for (uint32_t channel = 0; channel < 3; ++channel) {
                if (!probe::check(pixel[channel] > expected[face][channel] - 55 &&
                    pixel[channel] < expected[face][channel] + 55,
                    "KTX2 cubemap face color preserved")) return 1;
            }
        }
        std::fprintf(stdout, "basisu cubemap transcode: faces=6\n");
    }
    if (argc == 4) {
        std::ifstream alpha_input(argv[3], std::ios::binary);
        if (!probe::check(alpha_input.good(), "KTX2 alpha file readable")) return 1;
        const std::vector<uint8_t> alpha_bytes(
            (std::istreambuf_iterator<char>(alpha_input)), std::istreambuf_iterator<char>());
        basist::ktx2_transcoder alpha;
        if (!probe::check(alpha.init(alpha_bytes.data(), (uint32_t)alpha_bytes.size()),
            "KTX2 alpha texture parses")) return 1;
        if (!probe::check(alpha.get_width() == 4 && alpha.get_height() == 4 &&
            alpha.get_layers() == 0 && alpha.get_faces() == 1 && alpha.get_has_alpha() != 0 && alpha.is_srgb(),
            "KTX2 alpha and sRGB metadata preserved")) return 1;
        if (!probe::check(alpha.start_transcoding(), "KTX2 alpha starts transcoding")) return 1;
        std::vector<uint8_t> alpha_rgba(4 * 4 * 4);
        if (!probe::check(alpha.transcode_image_level(
            0, 0, 0, alpha_rgba.data(), 4 * 4,
            basist::transcoder_texture_format::cTFRGBA32, 0, 4, 4),
            "KTX2 alpha transcodes to RGBA")) return 1;
        if (!probe::check(alpha_rgba[3] >= 90 && alpha_rgba[3] <= 165,
            "KTX2 alpha values survive transcode")) return 1;
        uint8_t bc7_block[16] = {};
        if (!probe::check(alpha.transcode_image_level(
            0, 0, 0, bc7_block, 1, basist::transcoder_texture_format::cTFBC7_RGBA, 0, 1),
            "alpha KTX2 transcodes to one BC7 block")) return 1;
        uint8_t bc3_block[16] = {};
        if (!probe::check(alpha.transcode_image_level(
            0, 0, 0, bc3_block, 1, basist::transcoder_texture_format::cTFBC3_RGBA, 0, 1),
            "alpha KTX2 transcodes to one BC3 block")) return 1;
        std::fprintf(stdout, "basisu alpha transcode: first_alpha=%u\n", (unsigned)alpha_rgba[3]);
    }
    return 0;
}
