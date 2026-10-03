#pragma once
// Platform heap introspection used by the render scene's heap accounting.
#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ELISA_RENDER_SCENE_ASAN 1
#include <sanitizer/allocator_interface.h>
#endif
#endif
