#include "application_abi.h"
#include "shader_path_validation.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(ELISA_APPLICATION_TEST_PROBE)
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
    if (error || !std::filesystem::create_directories(root / "metal", error) || error) return 0;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};

    const std::filesystem::path binary = root / "metal/basic.cso";
    const std::filesystem::path manifest = root / "elisa.shader-manifest.json";
    const std::string original = "shader-binary";
    const std::string valid_manifest =
        "{\"files\":[{\"bytes\":13,\"path\":\"metal/basic.cso\","
        "\"sha256\":\"cab85ecb0184002dc5ee46b23bbf81615386a2bad26ea88d7590d10cb24d3771\"}],"
        "\"fingerprint\":\"b5b808c5601497db2b0f47bb006154e1dde8cef59aedd9486423313a95328206\",\"schema\":1}";
    const auto write_file = [](const std::filesystem::path& path, const std::string& bytes) {
        std::ofstream stream(path, std::ios::binary);
        if (!stream) return false;
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return bool(stream);
    };
    if (!write_file(binary, original) || !write_file(manifest, valid_manifest) ||
        !elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 0;

    if (!write_file(binary, "shader-Binary") ||
        elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 0;
    if (!write_file(binary, original) || !write_file(root / "metal/unlisted.cso", "extra") ||
        elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str())) return 0;
    std::filesystem::remove(root / "metal/unlisted.cso", error);
    if (error) return 0;
    const std::string stale_fingerprint = valid_manifest.substr(0, valid_manifest.find("b5b808c")) +
        std::string(64, '0') + "\",\"schema\":1}";
    return write_file(manifest, stale_fingerprint) &&
        !elisa::shader::root_is_valid(root.string().c_str(), manifest.string().c_str()) ? 1 : 0;
}
#endif
