#include "shader_path_validation.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

bool write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return bool(stream);
}

std::string manifest_json(uint64_t schema, const std::string& backend,
    const std::string& path, const std::string& bytes) {
    elisa::shader::ManifestFile file;
    file.path = path;
    file.bytes = bytes.size();
    elisa::assets::Sha256 file_hash;
    file_hash.update(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    file.sha256 = file_hash.finish();
    const std::vector<elisa::shader::ManifestFile> files = {file};
    const std::vector<std::string> backends = {backend};
    const std::string canonical = elisa::shader::canonical_fingerprint_payload(schema,
        schema == 1 ? std::vector<std::string>{} : backends, files);
    elisa::assets::Sha256 fingerprint_hash;
    fingerprint_hash.update(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size());
    const std::string fingerprint = fingerprint_hash.finish();
    const std::string backend_field = schema == 1 ? "" :
        "\"backends\":[\"" + backend + "\"],";
    return "{" + backend_field + "\"files\":[{\"bytes\":" +
        std::to_string(file.bytes) + ",\"path\":\"" + file.path + "\",\"sha256\":\"" +
        file.sha256 + "\"}],\"fingerprint\":\"" + fingerprint + "\",\"schema\":" +
        std::to_string(schema) + "}";
}

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("elisa-shader-manifest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    const std::string backend = elisa::shader::current_backend();
    const std::string other_backend = backend == "metal" ? "spirv" : "metal";
    const std::string extension = backend == "spirv" ? ".spv" : ".cso";
    if (!std::filesystem::create_directories(root / backend, error) || error) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};

    const std::filesystem::path binary = root / backend / ("basic" + extension);
    const std::filesystem::path manifest = root / "elisa.shader-manifest.json";
    const std::string original = "shader-binary";
    const std::string relative_path = backend + "/basic" + extension;
    const std::string schema_one = manifest_json(1, backend, relative_path, original);
    const std::string schema_two = manifest_json(2, backend, relative_path, original);
    const std::string wrong_backend = manifest_json(2, other_backend, relative_path, original);
    if (!write_file(binary, original) || !write_file(manifest, schema_one) ||
        !elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 2;
    if (!write_file(manifest, schema_two) ||
        !elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 3;
    std::string manifest_digest;
    elisa::assets::Sha256 expected_hash;
    expected_hash.update(reinterpret_cast<const uint8_t*>(schema_two.data()), schema_two.size());
    const std::string expected_manifest_digest = expected_hash.finish();
    if (!elisa::shader::manifest_content_digest(root.string().c_str(), manifest.string().c_str(), manifest_digest) ||
        manifest_digest != expected_manifest_digest) return 7;
#if defined(__APPLE__)
    const char* prior_key_value = std::getenv("WICKED_METAL_PIPELINE_ARCHIVE_SHADER_KEY");
    const bool had_prior_key = prior_key_value != nullptr;
    const std::string prior_key = had_prior_key ? prior_key_value : std::string();
    elisa::shader::configure_metal_pipeline_archive_shader_key(root.string().c_str(), manifest.string().c_str());
    const char* configured_key = std::getenv("WICKED_METAL_PIPELINE_ARCHIVE_SHADER_KEY");
    if (configured_key == nullptr || configured_key != expected_manifest_digest) return 9;
    elisa::shader::configure_metal_pipeline_archive_shader_key(root.string().c_str(), nullptr);
    if (std::getenv("WICKED_METAL_PIPELINE_ARCHIVE_SHADER_KEY") != nullptr) return 10;
    if (had_prior_key) setenv("WICKED_METAL_PIPELINE_ARCHIVE_SHADER_KEY", prior_key.c_str(), 1);
    else unsetenv("WICKED_METAL_PIPELINE_ARCHIVE_SHADER_KEY");
#endif
    if (!write_file(manifest, schema_one) ||
        !elisa::shader::manifest_content_digest(root.string().c_str(), manifest.string().c_str(), manifest_digest) ||
        manifest_digest == expected_manifest_digest) return 8;
    if (!write_file(manifest, wrong_backend) ||
        elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str()) ||
        elisa::shader::verify_shader_manifest(root, wrong_backend, other_backend)) return 4;
    if (!write_file(manifest, schema_two) ||
        !write_file(root / backend / ("unlisted" + extension), "extra") ||
        elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 5;
    if (elisa::shader::manifest_failure(root.string().c_str(), manifest.string().c_str()) !=
        "unlisted shader " + backend + "/unlisted" + extension) return 11;
    std::filesystem::remove(root / backend / ("unlisted" + extension), error);
    if (error || !write_file(binary, "shader-Binary") ||
        elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 6;
    if (elisa::shader::manifest_failure(root.string().c_str(), manifest.string().c_str()) !=
        "size or sha256 mismatch for " + relative_path) return 12;
    std::filesystem::remove(binary, error);
    if (error || elisa::shader::manifest_failure(root.string().c_str(), manifest.string().c_str()) !=
        "missing shader " + relative_path) return 13;
    if (elisa::shader::manifest_failure(root.string().c_str(), (root / "absent.json").string().c_str())
        .rfind("manifest is missing", 0) != 0) return 14;
    return 0;
}
