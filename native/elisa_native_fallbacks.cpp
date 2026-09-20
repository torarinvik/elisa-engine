// Weak no-op host hooks for Elisa runtime features the SDL3 host does not
// advertise. A host that provides callback services can override these symbols.
#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define ELISA_WEAK __attribute__((weak))
#else
#define ELISA_WEAK
#endif

extern "C" {

ELISA_WEAK void* elisa_native_callback_ptr(uint8_t*) { return nullptr; }
ELISA_WEAK uint32_t elisa_native_callback_call_u32_voidp(uint8_t*, void*, uint32_t fallback) { return fallback; }
ELISA_WEAK int32_t elisa_native_callback_call_i32_voidp(uint8_t*, void*, int32_t fallback) { return fallback; }
ELISA_WEAK uintptr_t elisa_native_callback_call_usize_voidp(uint8_t*, void*, uintptr_t fallback) { return fallback; }
ELISA_WEAK intptr_t elisa_native_callback_call_isize_voidp(uint8_t*, void*, intptr_t fallback) { return fallback; }
ELISA_WEAK uint32_t elisa_native_callback_spawn_join_u32_voidp(uint8_t*, void*, uint32_t fallback) { return fallback; }
ELISA_WEAK void* elisa_native_callback_context_new_u32_voidp(uint8_t*, void*, uint32_t) { return nullptr; }
ELISA_WEAK void* elisa_native_callback_context_entry_u32_voidp(void) { return nullptr; }
ELISA_WEAK int32_t elisa_native_callback_context_start_u32_voidp(void*, uintptr_t*) { return -1; }
ELISA_WEAK uint32_t elisa_native_callback_context_join_u32_voidp(uintptr_t, void*, uint32_t fallback) { return fallback; }
ELISA_WEAK uint32_t elisa_native_callback_context_spawn_join_u32_voidp(void*, uint32_t fallback) { return fallback; }
ELISA_WEAK uint32_t elisa_native_callback_context_result_u32(void*, uint32_t fallback) { return fallback; }
ELISA_WEAK void elisa_native_callback_context_free(void*) {}
ELISA_WEAK void* va_copy(void* source) { return source; }
ELISA_WEAK void va_end(void*) {}

}
