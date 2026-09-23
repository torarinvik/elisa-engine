#pragma once

// Bounded reader for the deterministic LOD-chain JSON emitted by the asset cooker.
#include "virtual_package.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace elisa::assets {

inline constexpr size_t MAX_LOD_LEVELS = 8;
inline constexpr size_t MAX_LOD_MANIFEST_BYTES = 1024 * 1024;
inline constexpr uint64_t MAX_LOD_PACKAGE_BYTES = probe::PackageIndex::MAX_PACKAGE_BYTES;

struct LodManifestLevel {
    uint64_t index = 0;
    std::string package;
    std::string sha256;
    uint64_t byte_size = 0;
    uint64_t triangles = 0;
    uint64_t vertices = 0;
    uint64_t attribute_bytes = 0;
    double simplify_ratio = 0.0;
    double relative_error = 0.0;
    double relative_error_budget = 0.0;
};

struct LodManifest {
    bool valid = false;
    std::string error;
    std::string asset_path;
    std::string source_sha256;
    double object_extent = 0.0;
    std::array<LodManifestLevel, MAX_LOD_LEVELS> levels{};
    size_t level_count = 0;
};

namespace lod_manifest_detail {

class JsonReader {
public:
    explicit JsonReader(std::string_view input) : input_(input) {}

    bool parse(LodManifest& manifest, std::string& error) {
        if (!parse_root(manifest)) {
            error = error_;
            return false;
        }
        whitespace();
        if (offset_ != input_.size()) return fail(error, "trailing LOD manifest data");
        if (!validate(manifest)) {
            error = error_;
            return false;
        }
        return true;
    }

private:
    std::string_view input_;
    size_t offset_ = 0;
    uint64_t declared_level_count_ = 0;
    std::string error_;

    bool fail(const char* message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    static bool fail(std::string& error, const char* message) {
        error = message;
        return false;
    }

    void whitespace() {
        while (offset_ < input_.size() && (input_[offset_] == ' ' || input_[offset_] == '\n' ||
            input_[offset_] == '\r' || input_[offset_] == '\t')) ++offset_;
    }

    bool consume(char expected) {
        whitespace();
        if (offset_ >= input_.size() || input_[offset_] != expected) return false;
        ++offset_;
        return true;
    }

    bool expect(char expected, const char* message) {
        return consume(expected) || fail(message);
    }

    bool mark(uint32_t& seen, uint32_t bit) {
        if ((seen & bit) != 0) return fail("duplicate LOD manifest field");
        seen |= bit;
        return true;
    }

    bool hex4(uint32_t& value) {
        if (input_.size() - offset_ < 4) return fail("truncated JSON unicode escape");
        value = 0;
        for (size_t index = 0; index < 4; ++index) {
            const char digit = input_[offset_++];
            uint32_t nibble = 0;
            if (digit >= '0' && digit <= '9') nibble = uint32_t(digit - '0');
            else if (digit >= 'a' && digit <= 'f') nibble = uint32_t(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F') nibble = uint32_t(digit - 'A' + 10);
            else return fail("invalid JSON unicode escape");
            value = (value << 4) | nibble;
        }
        return true;
    }

    static void append_utf8(std::string& output, uint32_t value) {
        if (value <= 0x7f) output.push_back(char(value));
        else if (value <= 0x7ff) {
            output.push_back(char(0xc0 | (value >> 6)));
            output.push_back(char(0x80 | (value & 0x3f)));
        } else if (value <= 0xffff) {
            output.push_back(char(0xe0 | (value >> 12)));
            output.push_back(char(0x80 | ((value >> 6) & 0x3f)));
            output.push_back(char(0x80 | (value & 0x3f)));
        } else {
            output.push_back(char(0xf0 | (value >> 18)));
            output.push_back(char(0x80 | ((value >> 12) & 0x3f)));
            output.push_back(char(0x80 | ((value >> 6) & 0x3f)));
            output.push_back(char(0x80 | (value & 0x3f)));
        }
    }

    bool read_string(std::string& value) {
        value.clear();
        if (!consume('"')) return fail("expected JSON string");
        while (offset_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[offset_++]);
            if (character == '"') return true;
            if (character < 0x20) return fail("control byte in JSON string");
            if (character != '\\') {
                value.push_back(char(character));
            } else {
                if (offset_ >= input_.size()) return fail("truncated JSON escape");
                const char escaped = input_[offset_++];
                if (escaped == '"' || escaped == '\\' || escaped == '/') value.push_back(escaped);
                else if (escaped == 'b') value.push_back('\b');
                else if (escaped == 'f') value.push_back('\f');
                else if (escaped == 'n') value.push_back('\n');
                else if (escaped == 'r') value.push_back('\r');
                else if (escaped == 't') value.push_back('\t');
                else if (escaped == 'u') {
                    uint32_t codepoint = 0;
                    if (!hex4(codepoint)) return false;
                    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                        if (input_.size() - offset_ < 6 || input_[offset_] != '\\' ||
                            input_[offset_ + 1] != 'u') return fail("unpaired JSON high surrogate");
                        offset_ += 2;
                        uint32_t low = 0;
                        if (!hex4(low) || low < 0xdc00 || low > 0xdfff) {
                            return fail("invalid JSON surrogate pair");
                        }
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                        return fail("unpaired JSON low surrogate");
                    }
                    append_utf8(value, codepoint);
                } else return fail("invalid JSON escape");
            }
            if (value.size() > 4096) return fail("LOD manifest string exceeds its bound");
        }
        return fail("unterminated JSON string");
    }

    bool read_number(double& value) {
        whitespace();
        const size_t start = offset_;
        if (offset_ < input_.size() && input_[offset_] == '-') ++offset_;
        if (offset_ >= input_.size()) return fail("invalid JSON number");
        if (input_[offset_] == '0') ++offset_;
        else {
            if (input_[offset_] < '1' || input_[offset_] > '9') return fail("invalid JSON number");
            while (offset_ < input_.size() && input_[offset_] >= '0' && input_[offset_] <= '9') ++offset_;
        }
        if (offset_ < input_.size() && input_[offset_] == '.') {
            ++offset_;
            const size_t digits = offset_;
            while (offset_ < input_.size() && input_[offset_] >= '0' && input_[offset_] <= '9') ++offset_;
            if (offset_ == digits) return fail("invalid JSON fraction");
        }
        if (offset_ < input_.size() && (input_[offset_] == 'e' || input_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < input_.size() && (input_[offset_] == '+' || input_[offset_] == '-')) ++offset_;
            const size_t digits = offset_;
            while (offset_ < input_.size() && input_[offset_] >= '0' && input_[offset_] <= '9') ++offset_;
            if (offset_ == digits) return fail("invalid JSON exponent");
        }
        const std::string token(input_.substr(start, offset_ - start));
        const char* first = token.data();
        const char* last = first + token.size();
        const auto parsed = std::from_chars(first, last, value, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != last || !std::isfinite(value)) {
            return fail("non-finite or invalid LOD manifest number");
        }
        return true;
    }

    bool read_unsigned(uint64_t& value) {
        whitespace();
        const size_t start = offset_;
        if (offset_ >= input_.size() || input_[offset_] < '0' || input_[offset_] > '9') {
            return fail("expected nonnegative LOD manifest integer");
        }
        if (input_[offset_] == '0') ++offset_;
        else while (offset_ < input_.size() && input_[offset_] >= '0' && input_[offset_] <= '9') ++offset_;
        if (offset_ < input_.size() && (input_[offset_] == '.' || input_[offset_] == 'e' ||
            input_[offset_] == 'E' || input_[offset_] == '+' || input_[offset_] == '-')) {
            return fail("LOD manifest count is not an integer");
        }
        const char* first = input_.data() + start;
        const char* last = input_.data() + offset_;
        const auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last) return fail("LOD manifest integer is out of range");
        return true;
    }

    bool parse_root(LodManifest& manifest) {
        if (!expect('{', "expected LOD manifest object")) return false;
        uint32_t seen = 0;
        bool first = true;
        while (!consume('}')) {
            if (!first && !expect(',', "expected LOD manifest field separator")) return false;
            first = false;
            std::string key;
            if (!read_string(key) || !expect(':', "expected LOD manifest field value")) return false;
            uint32_t bit = 0;
            if (key == "asset_path") bit = 1u << 0;
            else if (key == "error_metric") bit = 1u << 1;
            else if (key == "format") bit = 1u << 2;
            else if (key == "level_count") bit = 1u << 3;
            else if (key == "levels") bit = 1u << 4;
            else if (key == "object_extent") bit = 1u << 5;
            else if (key == "source_sha256") bit = 1u << 6;
            else return fail("unknown LOD manifest field");
            if (!mark(seen, bit)) return false;
            if (bit == (1u << 0)) {
                if (!read_string(manifest.asset_path)) return false;
            } else if (bit == (1u << 1) || bit == (1u << 2) || bit == (1u << 6)) {
                std::string value;
                if (!read_string(value)) return false;
                if (bit == (1u << 1) && value != "meshoptimizer-relative-geometric-error") {
                    return fail("unsupported LOD error metric");
                }
                if (bit == (1u << 2) && value != "elisa-lod-chain-v1") {
                    return fail("unsupported LOD manifest format");
                }
                if (bit == (1u << 0)) manifest.asset_path = std::move(value);
                else if (bit == (1u << 6)) manifest.source_sha256 = std::move(value);
            } else if (bit == (1u << 3)) {
                uint64_t count = 0;
                if (!read_unsigned(count) || count > MAX_LOD_LEVELS) return fail("invalid LOD level count");
                declared_level_count_ = count;
            } else if (bit == (1u << 4)) {
                if (!parse_levels(manifest)) return false;
            } else if (!read_number(manifest.object_extent)) return false;
        }
        if (seen != 0x7fu) return fail("LOD manifest is missing required fields");
        if (declared_level_count_ != manifest.level_count) return fail("LOD level count does not match its array");
        return true;
    }

    bool parse_levels(LodManifest& manifest) {
        if (!expect('[', "expected LOD levels array")) return false;
        if (consume(']')) return true;
        while (manifest.level_count < MAX_LOD_LEVELS) {
            if (!parse_level(manifest.levels[manifest.level_count])) return false;
            ++manifest.level_count;
            if (consume(']')) return true;
            if (!expect(',', "expected LOD level separator")) return false;
        }
        return fail("LOD chain exceeds its level bound");
    }

    bool parse_level(LodManifestLevel& level) {
        if (!expect('{', "expected LOD level object")) return false;
        uint32_t seen = 0;
        bool first = true;
        while (!consume('}')) {
            if (!first && !expect(',', "expected LOD level field separator")) return false;
            first = false;
            std::string key;
            if (!read_string(key) || !expect(':', "expected LOD level field value")) return false;
            uint32_t bit = 0;
            if (key == "attribute_bytes") bit = 1u << 0;
            else if (key == "byte_size") bit = 1u << 1;
            else if (key == "index") bit = 1u << 2;
            else if (key == "package") bit = 1u << 3;
            else if (key == "relative_error") bit = 1u << 4;
            else if (key == "relative_error_budget") bit = 1u << 5;
            else if (key == "sha256") bit = 1u << 6;
            else if (key == "simplify_ratio") bit = 1u << 7;
            else if (key == "triangles") bit = 1u << 8;
            else if (key == "vertices") bit = 1u << 9;
            else return fail("unknown LOD level field");
            if (!mark(seen, bit)) return false;
            if (bit == (1u << 3) || bit == (1u << 6)) {
                std::string value;
                if (!read_string(value)) return false;
                if (bit == (1u << 3)) level.package = std::move(value);
                else level.sha256 = std::move(value);
            } else if (bit == (1u << 4) || bit == (1u << 5) || bit == (1u << 7)) {
                double value = 0.0;
                if (!read_number(value)) return false;
                if (bit == (1u << 4)) level.relative_error = value;
                else if (bit == (1u << 5)) level.relative_error_budget = value;
                else level.simplify_ratio = value;
            } else {
                uint64_t value = 0;
                if (!read_unsigned(value)) return false;
                if (bit == (1u << 0)) level.attribute_bytes = value;
                else if (bit == (1u << 1)) level.byte_size = value;
                else if (bit == (1u << 2)) level.index = value;
                else if (bit == (1u << 8)) level.triangles = value;
                else level.vertices = value;
            }
        }
        if (seen != 0x3ffu) return fail("LOD level is missing required fields");
        return true;
    }

    static bool safe_relative_path(const std::string& path, bool basename) {
        if (path.empty() || path.size() > 4096 || path.front() == '/' || path.find('\\') != std::string::npos ||
            path.find('\0') != std::string::npos || path.find('\n') != std::string::npos ||
            path.find('\r') != std::string::npos || (basename && path.find('/') != std::string::npos)) return false;
        size_t start = 0;
        while (start <= path.size()) {
            const size_t end = path.find('/', start);
            const std::string_view part(path.data() + start,
                (end == std::string::npos ? path.size() : end) - start);
            if (part.empty() || part == "." || part == "..") return false;
            for (unsigned char character : part) if (character < 0x20) return false;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return true;
    }

    static bool valid_sha256(const std::string& value) {
        if (value.size() != 64) return false;
        for (char character : value) {
            if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) return false;
        }
        return true;
    }

    bool validate(LodManifest& manifest) {
        if (!safe_relative_path(manifest.asset_path, false) || !valid_sha256(manifest.source_sha256) ||
            !std::isfinite(manifest.object_extent) || manifest.object_extent < 0.0 ||
            manifest.level_count < 2 || manifest.level_count > MAX_LOD_LEVELS) {
            return fail("invalid LOD manifest identity, extent, or level count");
        }
        double error_budget = 0.0;
        double previous_ratio = 1.0;
        std::array<std::string, MAX_LOD_LEVELS> names{};
        std::array<std::string, MAX_LOD_LEVELS> digests{};
        for (size_t index = 0; index < manifest.level_count; ++index) {
            const LodManifestLevel& level = manifest.levels[index];
            const bool full_detail = index == 0;
            const bool package_suffix = level.package.size() >= 4 &&
                std::string_view(level.package).substr(level.package.size() - 4) == ".pkg";
            const bool bundle_suffix = level.package.size() >= 5 &&
                std::string_view(level.package).substr(level.package.size() - 5) == ".elpk";
            const std::string expected_tail = ".lod-0" + std::to_string(index) + "-" +
                level.sha256.substr(0, 16) + (package_suffix ? ".pkg" : ".elpk");
            const bool content_addressed_name = level.package.size() > expected_tail.size() &&
                level.package.compare(level.package.size() - expected_tail.size(),
                    expected_tail.size(), expected_tail) == 0;
            if (level.index != index || !safe_relative_path(level.package, true) ||
                (!package_suffix && !bundle_suffix) || !valid_sha256(level.sha256) ||
                !content_addressed_name ||
                level.byte_size == 0 || level.byte_size > MAX_LOD_PACKAGE_BYTES ||
                level.triangles == 0 || level.triangles > 5'000'000 ||
                level.vertices == 0 || level.vertices > 2'000'000 ||
                level.attribute_bytes > MAX_LOD_PACKAGE_BYTES ||
                !std::isfinite(level.simplify_ratio) || !std::isfinite(level.relative_error) ||
                !std::isfinite(level.relative_error_budget) || level.relative_error < 0.0 ||
                level.relative_error > 0.02 || level.relative_error_budget < error_budget ||
                level.relative_error_budget < level.relative_error ||
                (full_detail ? level.simplify_ratio != 1.0 || level.relative_error != 0.0 ||
                    level.relative_error_budget != 0.0 :
                    level.simplify_ratio <= 0.0 || level.simplify_ratio >= previous_ratio)) {
                return fail("invalid LOD level metadata or ordering");
            }
            error_budget = std::max(error_budget, level.relative_error);
            if (std::abs(level.relative_error_budget - error_budget) > 1.0e-12) {
                return fail("LOD error budget does not match measured levels");
            }
            for (size_t earlier = 0; earlier < index; ++earlier) {
                if (names[earlier] == level.package || digests[earlier] == level.sha256) {
                    return fail("LOD package names and hashes must be unique");
                }
            }
            names[index] = level.package;
            digests[index] = level.sha256;
            previous_ratio = level.simplify_ratio;
        }
        return true;
    }
};

} // namespace lod_manifest_detail

inline bool parse_lod_manifest(std::string_view bytes, LodManifest& manifest, std::string& error) {
    manifest = {};
    error.clear();
    if (bytes.empty() || bytes.size() > MAX_LOD_MANIFEST_BYTES) {
        error = "LOD manifest size rejected";
        return false;
    }
    lod_manifest_detail::JsonReader reader(bytes);
    if (!reader.parse(manifest, error)) {
        manifest = {};
        manifest.error = error;
        return false;
    }
    manifest.valid = true;
    return true;
}

inline LodManifest read_lod_manifest(const std::string& path) {
    LodManifest manifest;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        manifest.error = "LOD manifest could not be opened";
        return manifest;
    }
    const std::streampos end = input.tellg();
    if (end <= 0 || static_cast<uint64_t>(end) > MAX_LOD_MANIFEST_BYTES) {
        manifest.error = "LOD manifest size rejected";
        return manifest;
    }
    std::string bytes(static_cast<size_t>(end), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        manifest.error = "LOD manifest read failed";
        return manifest;
    }
    std::string error;
    if (!parse_lod_manifest(bytes, manifest, error)) manifest.error = std::move(error);
    return manifest;
}

} // namespace elisa::assets
