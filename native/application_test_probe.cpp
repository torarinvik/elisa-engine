#include "application_abi.h"
#include "shader_path_validation.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(ELISA_APPLICATION_TEST_PROBE)
namespace {

std::string shader_manifest_test_json(uint64_t schema, const std::string& backend,
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
    const std::string backend_field = schema == 1 ? "" :
        "\"backends\":[\"" + backend + "\"],";
    return "{" + backend_field + "\"files\":[{\"bytes\":" +
        std::to_string(file.bytes) + ",\"path\":\"" + file.path + "\",\"sha256\":\"" +
        file.sha256 + "\"}],\"fingerprint\":\"" + fingerprint_hash.finish() +
        "\",\"schema\":" + std::to_string(schema) + "}";
}

} // namespace

extern "C" int32_t elisa_application_v1_test_rejects_missing_shader_root(void) {
    const char* original = std::getenv("ELISA_ENGINE_SHADER_PATH");
    const bool had_original = original != nullptr;
    const std::string saved = had_original ? original : std::string();
    if (setenv("ELISA_ENGINE_SHADER_PATH", "/elisa/missing/shader/root", 1) != 0) return 0;
    const int32_t result = elisa_application_v1_initialize("shader-root-test", 320, 200, 1);
    if (had_original) setenv("ELISA_ENGINE_SHADER_PATH", saved.c_str(), 1);
    else unsetenv("ELISA_ENGINE_SHADER_PATH");
    if (result == ELISA_APPLICATION_OK) {
        elisa_application_v1_shutdown();
        return 0;
    }
    return result == ELISA_APPLICATION_SHADER_PATH_INVALID ? 1 : 0;
}

extern "C" int32_t elisa_application_v1_test_rejects_invalid_shader_manifest(void) {
    const char* original = std::getenv("ELISA_ENGINE_SHADER_MANIFEST");
    const bool had_original = original != nullptr;
    const std::string saved = had_original ? original : std::string();
    if (setenv("ELISA_ENGINE_SHADER_MANIFEST", "/elisa/missing/shader-manifest.json", 1) != 0) return 0;
    const int32_t result = elisa_application_v1_initialize("shader-manifest-test", 320, 200, 1);
    if (had_original) setenv("ELISA_ENGINE_SHADER_MANIFEST", saved.c_str(), 1);
    else unsetenv("ELISA_ENGINE_SHADER_MANIFEST");
    if (result == ELISA_APPLICATION_OK) {
        elisa_application_v1_shutdown();
        return 0;
    }
    return result == ELISA_APPLICATION_SHADER_MANIFEST_INVALID ? 1 : 0;
}

extern "C" int32_t elisa_application_v1_test_shader_manifest_verifies_contents(void) {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error) /
        ("elisa-shader-manifest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string backend = elisa::shader::current_backend();
    const std::string other_backend = backend == "metal" ? "spirv" : "metal";
    const std::string extension = backend == "spirv" ? ".spv" : ".cso";
    const std::string relative_shader = backend + "/basic" + extension;
    if (error || !std::filesystem::create_directories(root / backend, error) || error) return 0;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};

    const std::filesystem::path binary = root / relative_shader;
    const std::filesystem::path manifest = root / "elisa.shader-manifest.json";
    const std::string original = "shader-binary";
    const std::string valid_manifest = shader_manifest_test_json(2, backend, relative_shader, original);
    const std::string legacy_manifest = shader_manifest_test_json(1, backend, relative_shader, original);
    const std::string wrong_backend_manifest = shader_manifest_test_json(
        2, other_backend, relative_shader, original);
    const auto write_file = [](const std::filesystem::path& path, const std::string& bytes) {
        std::ofstream stream(path, std::ios::binary);
        if (!stream) return false;
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return bool(stream);
    };
    if (!write_file(binary, original) || !write_file(manifest, legacy_manifest) ||
        !elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str()) ||
        !write_file(manifest, valid_manifest) ||
        !elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 0;
    if (!write_file(manifest, wrong_backend_manifest) ||
        elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str()) ||
        elisa::shader::verify_shader_manifest(root, wrong_backend_manifest, other_backend)) return 0;

    if (!write_file(binary, "shader-Binary") ||
        elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 0;
    if (!write_file(binary, original) ||
        !write_file(root / backend / ("unlisted" + extension), "extra") ||
        elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 0;
    std::filesystem::remove(root / backend / ("unlisted" + extension), error);
    if (error) return 0;
    std::string stale_fingerprint = valid_manifest;
    const size_t fingerprint_begin = stale_fingerprint.find("\"fingerprint\":\"");
    if (fingerprint_begin == std::string::npos) return 0;
    const size_t digest_begin = fingerprint_begin + std::string("\"fingerprint\":\"").size();
    stale_fingerprint.replace(digest_begin, 64, std::string(64, '0'));
    return write_file(manifest, stale_fingerprint) &&
        !elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str()) ? 1 : 0;
}
#endif
