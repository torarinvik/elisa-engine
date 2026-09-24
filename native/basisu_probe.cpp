// CPU reference validation for cooked Basis KTX2 textures; runtime format
// selection and GPU upload are exercised by the Wicked native probe.
#include "probe_core.h"
#include "ktx2_format_policy.h"

#include "basisu_transcoder.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

struct KTX2HeaderMutation {
    const char* label;
    size_t offset;
    uint64_t value;
    size_t width;
};

static void write_little_endian(std::vector<uint8_t>& bytes, size_t offset, uint64_t value, size_t width) {
    for (size_t byte = 0; byte < width; ++byte) {
        bytes[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
    }
}

static bool check_malformed_ktx2_rejected(const std::vector<uint8_t>& valid) {
    const KTX2HeaderMutation mutations[] = {
        {"unsupported Vulkan format", 12, 37, 4},
        {"invalid texel type size", 16, 2, 4},
        {"zero image height", 24, 0, 4},
        {"3D image depth", 28, 1, 4},
        {"invalid face count", 36, 2, 4},
        {"zero mip count", 40, 0, 4},
        {"unsupported supercompression", 44, 3, 4},
        {"out-of-range DFD offset", 48, std::numeric_limits<uint32_t>::max(), 4},
        {"unsupported DFD size", 52, 43, 4},
        {"out-of-range key/value offset", 56, std::numeric_limits<uint32_t>::max(), 4},
        {"oversized key/value length", 60, std::numeric_limits<uint32_t>::max(), 4},
        {"out-of-range mip offset", 80, std::numeric_limits<uint64_t>::max(), 8},
        {"oversized mip payload", 88, std::numeric_limits<uint64_t>::max(), 8},
        {"oversized decoded mip length", 96, 2ull * 1024 * 1024 * 1024, 8},
        {"missing decoded Zstandard length", 96, 0, 8},
    };
    for (const KTX2HeaderMutation& mutation : mutations) {
        std::vector<uint8_t> corrupted = valid;
        write_little_endian(corrupted, mutation.offset, mutation.value, mutation.width);
        basist::ktx2_transcoder transcoder;
        if (!probe::check(!transcoder.init(corrupted.data(), static_cast<uint32_t>(corrupted.size())),
            mutation.label)) return false;
    }
    return true;
}

static bool check_ktx2_upload_shapes() {
    using elisa::rendering::textures::ktx2_upload_shape_supported;
    return probe::check(ktx2_upload_shape_supported(0, 1, 64, 32),
            "KTX2 upload accepts a plain 2D texture") &&
        probe::check(ktx2_upload_shape_supported(0, 6, 64, 64),
            "KTX2 upload accepts a square cubemap") &&
        probe::check(!ktx2_upload_shape_supported(1, 1, 64, 32),
            "KTX2 upload rejects a one-layer array it cannot preserve") &&
        probe::check(!ktx2_upload_shape_supported(2, 1, 64, 32),
            "KTX2 upload rejects texture arrays") &&
        probe::check(!ktx2_upload_shape_supported(0, 6, 64, 32),
            "KTX2 upload rejects nonsquare cubemaps") &&
        probe::check(!ktx2_upload_shape_supported(0, 2, 64, 32),
            "KTX2 upload rejects unsupported face counts") &&
        probe::check(!ktx2_upload_shape_supported(0, 1, 0, 32),
            "KTX2 upload rejects empty dimensions");
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 8) {
        std::fprintf(stderr, "usage: basisu-probe <texture.ktx2> [cubemap.ktx2 [alpha.ktx2 [normal.ktx2 [hdr.ktx2 [swizzled-normal.ktx2 [swizzled-srgb.ktx2]]]]]]\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!probe::check(input.good(), "KTX2 file readable")) {
        return 1;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    basist::basisu_transcoder_init();
    if (!check_ktx2_upload_shapes()) return 1;
    if (!check_malformed_ktx2_rejected(bytes)) return 1;
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
    if (argc >= 4) {
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
    if (argc >= 5) {
        std::ifstream normal_input(argv[4], std::ios::binary);
        if (!probe::check(normal_input.good(), "KTX2 normal-map file readable")) return 1;
        const std::vector<uint8_t> normal_bytes(
            (std::istreambuf_iterator<char>(normal_input)), std::istreambuf_iterator<char>());
        basist::ktx2_transcoder normal;
        if (!probe::check(normal.init(normal_bytes.data(), (uint32_t)normal_bytes.size()),
            "KTX2 normal map parses")) return 1;
        if (!probe::check(normal.get_width() == 4 && normal.get_height() == 4 &&
            !normal.is_srgb(), "KTX2 normal map carries linear data")) return 1;
        if (!probe::check(normal.start_transcoding(), "KTX2 normal map starts transcoding")) return 1;
        std::vector<uint8_t> normal_rgba(4 * 4 * 4);
        if (!probe::check(normal.transcode_image_level(
            0, 0, 0, normal_rgba.data(), 4 * 4,
            basist::transcoder_texture_format::cTFRGBA32, 0, 4, 4),
            "KTX2 normal map transcodes to RGBA")) return 1;
        if (!probe::check(normal_rgba[0] >= 65 && normal_rgba[0] <= 125 &&
            normal_rgba[1] >= 155 && normal_rgba[1] <= 225,
            "RGBA normal map preserves canonical X and Y channels")) return 1;
    }
    if (argc >= 6) {
        std::ifstream hdr_input(argv[5], std::ios::binary);
        if (!probe::check(hdr_input.good(), "KTX2 HDR file readable")) return 1;
        const std::vector<uint8_t> hdr_bytes(
            (std::istreambuf_iterator<char>(hdr_input)), std::istreambuf_iterator<char>());
        basist::ktx2_transcoder hdr;
        if (!probe::check(hdr.init(hdr_bytes.data(), static_cast<uint32_t>(hdr_bytes.size())) &&
            hdr.get_width() == 4 && hdr.get_height() == 4 && hdr.is_hdr() &&
            hdr.get_has_alpha() == 0 && !hdr.is_srgb(),
            "KTX2 retains linear HDR metadata")) return 1;
        if (!probe::check(hdr.start_transcoding(), "KTX2 HDR starts transcoding")) return 1;
        std::vector<uint16_t> rgba_half(4 * 4 * 4);
        if (!probe::check(hdr.transcode_image_level(
            0, 0, 0, rgba_half.data(), 4 * 4,
            basist::transcoder_texture_format::cTFRGBA_HALF, 0, 4, 4),
            "HDR KTX2 transcodes to half-float RGBA")) return 1;
        bool preserves_values_above_one = false;
        for (size_t index = 0; index < rgba_half.size(); ++index) {
            if (index % 4 != 3 && rgba_half[index] > 0x3C00u) {
                preserves_values_above_one = true;
                break;
            }
        }
        if (!probe::check(preserves_values_above_one,
            "HDR half-float pixels preserve values above one")) return 1;
        uint8_t bc6h_block[16] = {};
        if (!probe::check(hdr.transcode_image_level(
            0, 0, 0, bc6h_block, 1, basist::transcoder_texture_format::cTFBC6H, 0, 1),
            "HDR KTX2 transcodes to BC6H")) return 1;
        std::fprintf(stdout, "basisu HDR transcode: bc6h=1 rgba16f_dynamic_range=1\n");
    }
    if (argc >= 7) {
        std::ifstream normal_input(argv[6], std::ios::binary);
        if (!probe::check(normal_input.good(), "KTX2 swizzled normal file readable")) return 1;
        const std::vector<uint8_t> normal_bytes(
            (std::istreambuf_iterator<char>(normal_input)), std::istreambuf_iterator<char>());
        basist::ktx2_transcoder normal;
        if (!probe::check(normal.init(normal_bytes.data(), static_cast<uint32_t>(normal_bytes.size())) &&
            normal.get_width() == 4 && normal.get_height() == 4 && normal.get_has_alpha() != 0 &&
            !normal.is_srgb(), "KTX2 split-channel normal metadata is preserved")) return 1;
        const basisu::uint8_vec* swizzle = normal.find_key("KTXswizzle");
        if (!probe::check(swizzle != nullptr && swizzle->size() == 6 &&
            (*swizzle)[0] == 'r' && (*swizzle)[1] == 'a' && (*swizzle)[2] == '0' &&
            (*swizzle)[3] == '1' && (*swizzle)[4] == 0 && (*swizzle)[5] == 0,
            "KTX2 normal channel mapping is explicit")) return 1;
        if (!probe::check(normal.start_transcoding(), "KTX2 split-channel normal starts transcoding")) return 1;
        uint8_t bc5_block[16] = {};
        if (!probe::check(normal.transcode_image_level(
            0, 0, 0, bc5_block, 1, basist::transcoder_texture_format::cTFBC5_RG, 0, 1),
            "split-channel normal transcodes to BC5")) return 1;
        basist::color_rgba bc5_pixels[16];
        basist::bcu::unpack_bc5(bc5_block, bc5_pixels);
        if (!probe::check(bc5_pixels[0].r >= 65 && bc5_pixels[0].r <= 125 &&
            bc5_pixels[0].g >= 155 && bc5_pixels[0].g <= 225,
            "BC5 retains normal X from red and Y from alpha")) return 1;
        std::fprintf(stdout, "basisu normal BC5 transcode: xy=%u,%u\n",
            (unsigned)bc5_pixels[0].r, (unsigned)bc5_pixels[0].g);
    }
    if (argc == 8) {
        std::ifstream color_input(argv[7], std::ios::binary);
        if (!probe::check(color_input.good(), "KTX2 swizzled sRGB file readable")) return 1;
        const std::vector<uint8_t> color_bytes(
            (std::istreambuf_iterator<char>(color_input)), std::istreambuf_iterator<char>());
        basist::ktx2_transcoder color;
        if (!probe::check(color.init(color_bytes.data(), static_cast<uint32_t>(color_bytes.size())) &&
            color.get_width() == 4 && color.get_height() == 4 && color.get_has_alpha() != 0 &&
            color.is_srgb(), "KTX2 swizzled color retains sRGB and alpha metadata")) return 1;
        const basisu::uint8_vec* swizzle = color.find_key("KTXswizzle");
        if (!probe::check(swizzle != nullptr && swizzle->size() == 6 &&
            (*swizzle)[0] == 'a' && (*swizzle)[1] == 'r' && (*swizzle)[2] == '0' &&
            (*swizzle)[3] == '1' && (*swizzle)[4] == 0 && (*swizzle)[5] == 0,
            "KTX2 sRGB channel mapping is explicit")) return 1;
        if (!probe::check(color.start_transcoding(), "KTX2 swizzled sRGB starts transcoding")) return 1;
        uint8_t rgba[4 * 4 * 4] = {};
        if (!probe::check(color.transcode_image_level(0, 0, 0, rgba, 4 * 4,
            basist::transcoder_texture_format::cTFRGBA32, 0, 4, 4),
            "KTX2 swizzled sRGB transcodes to RGBA")) return 1;
        if (!probe::check(rgba[0] >= 115 && rgba[0] <= 145 && rgba[3] >= 175 && rgba[3] <= 210,
            "KTX2 source retains authored encoded red and alpha")) return 1;
        std::fprintf(stdout, "basisu swizzled sRGB transcode: encoded=%u alpha=%u\n",
            (unsigned)rgba[0], (unsigned)rgba[3]);
    }
    return 0;
}
