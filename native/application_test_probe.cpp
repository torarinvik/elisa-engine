#include "application_abi.h"

#include <cstdlib>
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
#endif
