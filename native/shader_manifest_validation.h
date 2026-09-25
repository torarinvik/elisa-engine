#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "sha256_file.h"

namespace elisa::shader {

constexpr size_t MAX_MANIFEST_BYTES = 4 * 1024 * 1024;
constexpr size_t MAX_MANIFEST_FILES = 16384;
constexpr uint64_t MAX_SHADER_FILE_BYTES = 1024ull * 1024ull * 1024ull;
constexpr uint64_t MAX_SHADER_TOTAL_BYTES = 4ull * 1024ull * 1024ull * 1024ull;

struct ManifestFile {
    std::string path;
    uint64_t bytes = 0;
    std::string sha256;
};

inline void append_utf8(std::string& output, uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        output.push_back(char(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(char(0xc0 | (codepoint >> 6)));
        output.push_back(char(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(char(0xe0 | (codepoint >> 12)));
        output.push_back(char(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(char(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back(char(0xf0 | (codepoint >> 18)));
        output.push_back(char(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(char(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(char(0x80 | (codepoint & 0x3f)));
    }
}

inline bool next_utf8_codepoint(const std::string& value, size_t& offset, uint32_t& codepoint) {
    if (offset >= value.size()) return false;
    const uint8_t first = uint8_t(value[offset++]);
    if (first <= 0x7f) {
        codepoint = first;
        return true;
    }
    size_t continuation_count = 0;
    if (first >= 0xc2 && first <= 0xdf) {
        codepoint = first & 0x1f;
        continuation_count = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
        codepoint = first & 0x0f;
        continuation_count = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
        codepoint = first & 0x07;
        continuation_count = 3;
    } else {
        return false;
    }
    if (offset + continuation_count > value.size()) return false;
    for (size_t index = 0; index < continuation_count; ++index) {
        const uint8_t next = uint8_t(value[offset++]);
        if ((next & 0xc0) != 0x80) return false;
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    return !((continuation_count == 1 && codepoint < 0x80) ||
        (continuation_count == 2 && codepoint < 0x800) ||
        (continuation_count == 3 && codepoint < 0x10000) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff);
}

inline bool valid_utf8(const std::string& value) {
    size_t offset = 0;
    uint32_t codepoint = 0;
    while (offset < value.size()) {
        if (!next_utf8_codepoint(value, offset, codepoint)) return false;
    }
    return true;
}

class ManifestJsonReader {
public:
    explicit ManifestJsonReader(const std::string& input) : input_(input) {}

    bool read(uint64_t& schema, std::vector<std::string>& backends,
        std::vector<ManifestFile>& files, std::string& fingerprint) {
        if (!expect('{')) return false;
        bool has_schema = false;
        bool has_backends = false;
        bool has_files = false;
        bool has_fingerprint = false;
        for (;;) {
            skip_space();
            if (take('}')) break;
            std::string key;
            if (!read_string(key) || !expect(':')) return false;
            if (key == "schema" && !has_schema) {
                if (!read_uint(schema)) return false;
                has_schema = true;
            } else if (key == "backends" && !has_backends) {
                if (!read_strings(backends)) return false;
                has_backends = true;
            } else if (key == "files" && !has_files) {
                if (!read_files(files)) return false;
                has_files = true;
            } else if (key == "fingerprint" && !has_fingerprint) {
                if (!read_string(fingerprint)) return false;
                has_fingerprint = true;
            } else {
                return false;
            }
            skip_space();
            if (take('}')) break;
            if (!take(',')) return false;
        }
        skip_space();
        return offset_ == input_.size() && has_schema && has_files && has_fingerprint &&
            (schema == 1 ? !has_backends : schema == 2 && has_backends);
    }

private:
    const std::string& input_;
    size_t offset_ = 0;

    void skip_space() {
        while (offset_ < input_.size() && (input_[offset_] == ' ' || input_[offset_] == '\t' ||
            input_[offset_] == '\r' || input_[offset_] == '\n')) ++offset_;
    }

    bool take(char expected) {
        if (offset_ >= input_.size() || input_[offset_] != expected) return false;
        ++offset_;
        return true;
    }

    bool expect(char expected) {
        skip_space();
        return take(expected);
    }

    bool read_hex_quad(uint32_t& value) {
        if (offset_ + 4 > input_.size()) return false;
        value = 0;
        for (size_t index = 0; index < 4; ++index) {
            const char digit = input_[offset_++];
            uint32_t nibble = 0;
            if (digit >= '0' && digit <= '9') nibble = uint32_t(digit - '0');
            else if (digit >= 'a' && digit <= 'f') nibble = uint32_t(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F') nibble = uint32_t(digit - 'A' + 10);
            else return false;
            value = (value << 4) | nibble;
        }
        return true;
    }

    bool read_string(std::string& value) {
        skip_space();
        if (!take('"')) return false;
        value.clear();
        while (offset_ < input_.size()) {
            const uint8_t byte = uint8_t(input_[offset_++]);
            if (byte == '"') return valid_utf8(value);
            if (byte < 0x20) return false;
            if (byte != '\\') {
                value.push_back(char(byte));
                continue;
            }
            if (offset_ >= input_.size()) return false;
            const char escaped = input_[offset_++];
            if (escaped == '"' || escaped == '\\' || escaped == '/') value.push_back(escaped);
            else if (escaped == 'b') value.push_back('\b');
            else if (escaped == 'f') value.push_back('\f');
            else if (escaped == 'n') value.push_back('\n');
            else if (escaped == 'r') value.push_back('\r');
            else if (escaped == 't') value.push_back('\t');
            else if (escaped == 'u') {
                uint32_t codepoint = 0;
                if (!read_hex_quad(codepoint)) return false;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (offset_ + 2 > input_.size() || input_[offset_++] != '\\' ||
                        input_[offset_++] != 'u') return false;
                    uint32_t low = 0;
                    if (!read_hex_quad(low) || low < 0xdc00 || low > 0xdfff) return false;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return false;
                }
                append_utf8(value, codepoint);
            } else {
                return false;
            }
        }
        return false;
    }

    bool read_uint(uint64_t& value) {
        skip_space();
        const size_t begin = offset_;
        if (offset_ >= input_.size() || input_[offset_] < '0' || input_[offset_] > '9') return false;
        if (input_[offset_] == '0') ++offset_;
        else while (offset_ < input_.size() && input_[offset_] >= '0' && input_[offset_] <= '9') ++offset_;
        if (offset_ < input_.size() && ((input_[offset_] >= '0' && input_[offset_] <= '9') ||
            input_[offset_] == '.' || input_[offset_] == 'e' || input_[offset_] == 'E')) return false;
        const char* first = input_.data() + begin;
        const char* last = input_.data() + offset_;
        const auto parsed = std::from_chars(first, last, value);
        return parsed.ec == std::errc{} && parsed.ptr == last;
    }

    bool read_file(ManifestFile& file) {
        if (!expect('{')) return false;
        bool has_path = false;
        bool has_bytes = false;
        bool has_digest = false;
        for (;;) {
            skip_space();
            if (take('}')) break;
            std::string key;
            if (!read_string(key) || !expect(':')) return false;
            if (key == "path" && !has_path) {
                if (!read_string(file.path)) return false;
                has_path = true;
            } else if (key == "bytes" && !has_bytes) {
                if (!read_uint(file.bytes)) return false;
                has_bytes = true;
            } else if (key == "sha256" && !has_digest) {
                if (!read_string(file.sha256)) return false;
                has_digest = true;
            } else {
                return false;
            }
            skip_space();
            if (take('}')) break;
            if (!take(',')) return false;
        }
        return has_path && has_bytes && has_digest;
    }

    bool read_files(std::vector<ManifestFile>& files) {
        if (!expect('[')) return false;
        skip_space();
        if (take(']')) return true;
        for (;;) {
            if (files.size() >= MAX_MANIFEST_FILES) return false;
            ManifestFile file;
            if (!read_file(file)) return false;
            files.push_back(std::move(file));
            skip_space();
            if (take(']')) return true;
            if (!take(',')) return false;
        }
    }

    bool read_strings(std::vector<std::string>& values) {
        if (!expect('[')) return false;
        skip_space();
        if (take(']')) return true;
        for (;;) {
            if (values.size() >= 3) return false;
            std::string value;
            if (!read_string(value)) return false;
            values.push_back(std::move(value));
            skip_space();
            if (take(']')) return true;
            if (!take(',')) return false;
        }
    }
};

inline void append_python_json_string(std::string& output, const std::string& value) {
    static constexpr char HEX[] = "0123456789abcdef";
    output.push_back('"');
    size_t offset = 0;
    uint32_t codepoint = 0;
    while (next_utf8_codepoint(value, offset, codepoint)) {
        if (codepoint == '"' || codepoint == '\\') {
            output.push_back('\\');
            output.push_back(char(codepoint));
        } else if (codepoint == '\b') output += "\\b";
        else if (codepoint == '\f') output += "\\f";
        else if (codepoint == '\n') output += "\\n";
        else if (codepoint == '\r') output += "\\r";
        else if (codepoint == '\t') output += "\\t";
        else if (codepoint < 0x20 || codepoint > 0x7e) {
            const auto append_quad = [&](uint32_t quad) {
                output += "\\u";
                output.push_back(HEX[(quad >> 12) & 0xf]);
                output.push_back(HEX[(quad >> 8) & 0xf]);
                output.push_back(HEX[(quad >> 4) & 0xf]);
                output.push_back(HEX[quad & 0xf]);
            };
            if (codepoint <= 0xffff) append_quad(codepoint);
            else {
                const uint32_t scalar = codepoint - 0x10000;
                append_quad(0xd800 + (scalar >> 10));
                append_quad(0xdc00 + (scalar & 0x3ff));
            }
        } else output.push_back(char(codepoint));
    }
    output.push_back('"');
}

inline bool valid_digest(const std::string& digest) {
    return digest.size() == 64 && digest.find_first_not_of("0123456789abcdef") == std::string::npos;
}

inline bool safe_shader_path(const std::string& value) {
    if (value.empty() || value.front() == '/' || value.find('\\') != std::string::npos ||
        value.find('\0') != std::string::npos || !valid_utf8(value)) return false;
    std::filesystem::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    size_t begin = 0;
    while (begin <= value.size()) {
        const size_t end = value.find('/', begin);
        const size_t length = (end == std::string::npos ? value.size() : end) - begin;
        const std::string segment = value.substr(begin, length);
        if (segment.empty() || segment == "." || segment == "..") return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return char(std::tolower(character)); });
    return extension == ".cso" || extension == ".spv";
}

inline bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& path) {
    const std::filesystem::path relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& segment : relative) if (segment == "..") return false;
    return true;
}

inline std::string canonical_fingerprint_payload(uint64_t schema,
    const std::vector<std::string>& backends, const std::vector<ManifestFile>& files) {
    std::string canonical = "{";
    if (schema == 2) {
        canonical += "\"backends\":[";
        for (size_t index = 0; index < backends.size(); ++index) {
            if (index > 0) canonical.push_back(',');
            append_python_json_string(canonical, backends[index]);
        }
        canonical += "],";
    }
    canonical += "\"files\":[";
    for (size_t index = 0; index < files.size(); ++index) {
        if (index > 0) canonical.push_back(',');
        canonical += "{\"bytes\":" + std::to_string(files[index].bytes) + ",\"path\":";
        append_python_json_string(canonical, files[index].path);
        canonical += ",\"sha256\":";
        append_python_json_string(canonical, files[index].sha256);
        canonical.push_back('}');
    }
    canonical += "],\"schema\":" + std::to_string(schema);
    canonical.push_back('}');
    return canonical;
}

inline bool valid_shader_backend(const std::string& backend) {
    return backend == "hlsl6" || backend == "metal" || backend == "spirv";
}

inline bool shader_file_backend(const std::string& path, std::string& backend) {
    const size_t separator = path.find('/');
    if (separator == std::string::npos) return false;
    backend = path.substr(0, separator);
    if (!valid_shader_backend(backend)) return false;
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return char(std::tolower(character)); });
    return (backend == "spirv" && extension == ".spv") ||
        ((backend == "metal" || backend == "hlsl6") && extension == ".cso");
}

inline bool verify_shader_manifest(const std::filesystem::path& root, const std::string& content,
    const std::string& expected_backend) {
    uint64_t schema = 0;
    std::vector<std::string> backends;
    std::vector<ManifestFile> files;
    std::string fingerprint;
    ManifestJsonReader reader(content);
    if (!reader.read(schema, backends, files, fingerprint) || (schema != 1 && schema != 2) || files.empty() ||
        files.size() > MAX_MANIFEST_FILES || !valid_digest(fingerprint)) return false;

    if (!valid_shader_backend(expected_backend)) return false;
    if (schema == 2) {
        if (backends.empty() || backends.size() > 3 ||
            !std::is_sorted(backends.begin(), backends.end()) ||
            std::adjacent_find(backends.begin(), backends.end()) != backends.end()) return false;
        for (const std::string& backend : backends) if (!valid_shader_backend(backend)) return false;
        if (!std::binary_search(backends.begin(), backends.end(), expected_backend)) return false;
    }

    uint64_t total_bytes = 0;
    std::string previous_path;
    std::set<std::string> expected_paths;
    std::set<std::string> observed_backends;
    std::error_code error;
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) return false;
    for (const ManifestFile& file : files) {
        std::string backend;
        if (!safe_shader_path(file.path) || !shader_file_backend(file.path, backend) ||
            !valid_digest(file.sha256) || file.bytes == 0 ||
            file.bytes > MAX_SHADER_FILE_BYTES || (!previous_path.empty() && file.path <= previous_path) ||
            file.bytes > MAX_SHADER_TOTAL_BYTES - total_bytes || !expected_paths.insert(file.path).second) {
            return false;
        }
        previous_path = file.path;
        observed_backends.insert(backend);
        total_bytes += file.bytes;
        const std::filesystem::path candidate = canonical_root / std::filesystem::path(file.path);
        const std::filesystem::path canonical_file = std::filesystem::weakly_canonical(candidate, error);
        if (error || !path_is_within(canonical_root, canonical_file) ||
            !std::filesystem::is_regular_file(canonical_file, error) || error) return false;
        std::string digest;
        std::string hash_error;
        if (!elisa::assets::sha256_file(canonical_file, file.bytes, digest, hash_error) || digest != file.sha256) {
            return false;
        }
    }
    if (schema == 1) {
        if (observed_backends.find(expected_backend) == observed_backends.end()) return false;
    } else if (std::vector<std::string>(observed_backends.begin(), observed_backends.end()) != backends) {
        return false;
    }

    const std::string canonical = canonical_fingerprint_payload(schema, backends, files);
    elisa::assets::Sha256 hash;
    hash.update(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size());
    if (hash.finish() != fingerprint) return false;

    for (std::filesystem::recursive_directory_iterator iterator(canonical_root,
        std::filesystem::directory_options::skip_permission_denied, error), end;
        iterator != end; iterator.increment(error)) {
        if (error) return false;
        if (iterator->is_regular_file(error) && !error) {
            std::string extension = iterator->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char character) { return char(std::tolower(character)); });
            if (extension == ".cso" || extension == ".spv") {
                const std::string relative = iterator->path().lexically_relative(canonical_root).generic_string();
                if (expected_paths.erase(relative) != 1) return false;
            }
        }
    }
    return !error && expected_paths.empty();
}

} // namespace elisa::shader
