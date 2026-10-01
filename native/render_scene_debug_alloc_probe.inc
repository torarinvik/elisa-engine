// R06 test probe: counts heap allocations made while the debug-draw scope is
// armed on this thread. The render path arms it around its debug flush step, so
// a frame with debug drawing disabled (nothing queued) must add zero. Only test
// builds replace the global operator new; it forwards to malloc either way.
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
#include <atomic>
#include <cstdlib>
#include <new>

namespace debug_alloc_probe {
inline std::atomic<uint64_t> allocations{0};
inline thread_local bool armed = false;
struct Scope {
    bool previous;
    Scope() : previous(armed) { armed = true; }
    ~Scope() { armed = previous; }
};
} // namespace debug_alloc_probe

void* operator new(std::size_t size) {
    if (debug_alloc_probe::armed) debug_alloc_probe::allocations.fetch_add(1);
    void* pointer = std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) throw std::bad_alloc();
    return pointer;
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

extern "C" uint64_t elisa_render_scene_v1_test_debug_draw_allocations(void) {
    return debug_alloc_probe::allocations.load();
}

// Sanity control: one heap string built inside an armed scope must count.
extern "C" uint64_t elisa_render_scene_v1_test_debug_alloc_sanity(void) {
    const uint64_t before = debug_alloc_probe::allocations.load();
    {
        debug_alloc_probe::Scope scope;
        std::string* value = new std::string(64, 'x');
        delete value;
    }
    return debug_alloc_probe::allocations.load() - before;
}
#define ELISA_DEBUG_ALLOC_SCOPE debug_alloc_probe::Scope debug_alloc_scope
#else
#define ELISA_DEBUG_ALLOC_SCOPE do {} while (false)
#endif
