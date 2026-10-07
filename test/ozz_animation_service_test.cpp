// C02: the ozz animation service loads the committed cooked elisa-anim-v1 rig,
// samples it like the engine's linear reference within ozz key quantization,
// refuses tampered images, and samples many characters with independent
// clips and times without a single heap allocation per tick (global operator
// new and the ozz allocator are both counted).
#include "../native/elisa_anim_v1.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>

#include "ozz/base/memory/allocator.h"

namespace {
std::atomic<uint64_t> g_new_calls{0};
std::atomic<uint64_t> g_ozz_calls{0};

class CountingAllocator : public ozz::memory::Allocator {
public:
    explicit CountingAllocator(ozz::memory::Allocator* inner) : inner_(inner) {}
    void* Allocate(size_t size, size_t alignment) override {
        g_ozz_calls.fetch_add(1);
        return inner_->Allocate(size, alignment);
    }
    void Deallocate(void* block) override { inner_->Deallocate(block); }
private:
    ozz::memory::Allocator* inner_;
};

int g_failures = 0;
void expect(bool condition, const char* what) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_failures; }
}
} // namespace

void* operator new(std::size_t size) {
    g_new_calls.fetch_add(1);
    void* pointer = std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) throw std::bad_alloc();
    return pointer;
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace anim = elisa::animation;

namespace {
std::vector<uint8_t> read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void put_word(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (size_t k = 0; k < 4; ++k) bytes[at + k] = uint8_t(value >> (8 * k));
}
void reseal(std::vector<uint8_t>& bytes) { put_word(bytes, 12, anim::anim_v1::detail::fnv(bytes, 16)); }

// Engine reference: linear translation/scale, shortest-path nlerp rotation.
void reference(const std::vector<uint8_t>& bytes, size_t joints, size_t clip_at, float seconds, float* out) {
    uint32_t tps = 0, tracks = 0;
    anim::anim_v1::detail::word(bytes, clip_at + 12, tps);
    anim::anim_v1::detail::word(bytes, clip_at + 16, tracks);
    size_t at = clip_at + 24;
    for (size_t joint = 0; joint < joints; ++joint) {
        uint32_t keys = 0;
        anim::anim_v1::detail::word(bytes, at + 4, keys);
        at += 8;
        float a[10], b[10];
        size_t chosen = 0;
        for (size_t key = 0; key < keys; ++key) {
            uint32_t tick = 0;
            anim::anim_v1::detail::word(bytes, at + key * 44, tick);
            if (float(tick) / float(tps) <= seconds) chosen = key;
        }
        const size_t next = std::min<size_t>(chosen + 1, keys - 1);
        uint32_t t0 = 0, t1 = 0;
        anim::anim_v1::detail::word(bytes, at + chosen * 44, t0);
        anim::anim_v1::detail::word(bytes, at + next * 44, t1);
        anim::anim_v1::detail::floats(bytes, at + chosen * 44 + 4, a, 10);
        anim::anim_v1::detail::floats(bytes, at + next * 44 + 4, b, 10);
        const float w = t1 > t0 ? std::clamp((seconds - float(t0) / tps) / (float(t1 - t0) / tps), 0.0f, 1.0f) : 0.0f;
        float* o = out + joint * 10;
        float dot = 0;
        for (int c = 3; c < 7; ++c) dot += a[c] * b[c];
        const float sign = dot < 0 ? -1.0f : 1.0f;
        float len = 0;
        for (int c = 0; c < 10; ++c) o[c] = a[c] + ((c >= 3 && c < 7 ? b[c] * sign : b[c]) - a[c]) * w;
        for (int c = 3; c < 7; ++c) len += o[c] * o[c];
        for (int c = 3; c < 7; ++c) o[c] /= std::sqrt(len);
        at += size_t(keys) * 44;
    }
}

bool pose_close(const float* got, const float* want, size_t joints, float* worst) {
    *worst = 0.0f;
    for (size_t joint = 0; joint < joints; ++joint) {
        const float* g = got + joint * 10;
        const float* w = want + joint * 10;
        float dot = 0;
        for (int c = 3; c < 7; ++c) dot += g[c] * w[c];
        const float sign = dot < 0 ? -1.0f : 1.0f;
        for (int c = 0; c < 10; ++c) {
            const float value = (c >= 3 && c < 7) ? g[c] * sign : g[c];
            const float error = std::fabs(value - w[c]) / (c >= 3 && c < 7 ? 1.0f : std::max(1.0f, std::fabs(w[c])));
            *worst = std::max(*worst, error);
        }
    }
    return *worst < 2.0e-3f;
}

// A synthetic 64-joint chain with three fixed-rate clips, to stress the
// service at the format's joint limit.
struct Synthetic {
    anim::OzzRig rig;
    std::vector<anim::OzzClip> clips;
};
Synthetic synthetic_rig() {
    constexpr size_t joints = 64, frames = 61;
    std::vector<int32_t> parents(joints);
    std::vector<float> rest(joints * 10);
    for (size_t j = 0; j < joints; ++j) {
        parents[j] = j == 0 ? -1 : int32_t((j - 1) / 2);
        float* p = rest.data() + j * 10;
        p[0] = 0; p[1] = 0.1f; p[2] = 0; p[3] = 0; p[4] = 0; p[5] = 0; p[6] = 1; p[7] = p[8] = p[9] = 1;
    }
    Synthetic out;
    out.rig = anim::build_rig(parents.data(), rest.data(), joints);
    for (int clip = 0; clip < 3; ++clip) {
        std::vector<float> data(frames * joints * 10);
        for (size_t f = 0; f < frames; ++f) {
            for (size_t j = 0; j < joints; ++j) {
                float* p = data.data() + (f * joints + j) * 10;
                const float angle = 0.02f * float(f) * float(clip + 1) + 0.01f * float(j);
                p[0] = 0.01f * float(f); p[1] = 0.1f; p[2] = 0;
                p[3] = 0; p[4] = std::sin(angle * 0.5f); p[5] = 0; p[6] = std::cos(angle * 0.5f);
                p[7] = p[8] = p[9] = 1.0f + 0.001f * float(f);
            }
        }
        out.clips.push_back(anim::build_fixed_rate_clip(out.rig, data.data(), frames, 30.0f, 2.0f));
    }
    return out;
}
} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "examples/character_course/rigs/guide_rig.anim";
    CountingAllocator counting(ozz::memory::default_allocator());
    ozz::memory::SetDefaulAllocator(&counting);

    const std::vector<uint8_t> bytes = read_file(path);
    expect(!bytes.empty(), "guide rig contract is readable");
    const anim::anim_v1::Package package = anim::anim_v1::load(bytes);
    expect(package.valid() && package.clips.size() == 1, "guide rig loads with one clip");
    if (!package.valid() || package.clips.empty()) return 1;
    const size_t joints = package.rig.joint_count();

    // Accuracy against the linear reference across the clip, between keys.
    {
        anim::OzzPoseContext context;
        context.prepare(package.rig, int(joints));
        std::vector<float> got(joints * 10), want(joints * 10);
        const size_t clip_at = 32 + joints * 112;
        float worst_overall = 0.0f;
        bool all_close = true;
        for (int step = 0; step <= 40; ++step) {
            const float t = package.clips[0].duration_seconds * float(step) / 40.0f + 0.0037f * float(step % 3);
            const float clamped = std::min(t, package.clips[0].duration_seconds);
            expect(context.sample(package.rig, package.clips[0].ozz, clamped, got.data()), "guide sample runs");
            reference(bytes, joints, clip_at, clamped, want.data());
            float worst = 0;
            all_close = pose_close(got.data(), want.data(), joints, &worst) && all_close;
            worst_overall = std::max(worst_overall, worst);
        }
        std::printf("ozz service: guide rig %zu joints, worst error vs linear reference %.2e\n", joints, worst_overall);
        expect(all_close, "ozz pose matches the linear reference within key quantization");
    }

    // Tampered images are refused.
    {
        std::vector<uint8_t> flipped = bytes;
        flipped[40] ^= 0x01;
        expect(!anim::anim_v1::load(flipped).valid(), "checksum mismatch refused");
        std::vector<uint8_t> rig_id = bytes;
        put_word(rig_id, 16, 1);
        reseal(rig_id);
        expect(!anim::anim_v1::load(rig_id).valid(), "stale rig identity refused");
        std::vector<uint8_t> units = bytes;
        put_word(units, 28, 0x40000000u); // 2 metres per unit is not the engine contract.
        reseal(units);
        expect(!anim::anim_v1::load(units).valid(), "non-engine animation units refused");
        std::vector<uint8_t> parent = bytes;
        put_word(parent, 32 + 4, 2); // root claims a later parent
        reseal(parent);
        expect(!anim::anim_v1::load(parent).valid(), "forward parent refused");
        std::vector<uint8_t> truncated(bytes.begin(), bytes.end() - 8);
        put_word(truncated, 8, uint32_t(truncated.size()));
        reseal(truncated);
        expect(!anim::anim_v1::load(truncated).valid(), "truncated clip refused");
        std::vector<uint8_t> track = bytes;
        put_word(track, 32 + joints * 112 + 24, 1); // first track names joint 1
        reseal(track);
        expect(!anim::anim_v1::load(track).valid(), "out-of-order track refused");
        std::vector<uint8_t> joint = bytes;
        uint32_t first_joint_id = 0;
        anim::anim_v1::detail::word(joint, 32, first_joint_id);
        put_word(joint, 32 + 112, first_joint_id);
        put_word(joint, 16, anim::anim_v1::detail::fnv(joint, 32, 32 + joints * 112));
        reseal(joint);
        expect(!anim::anim_v1::load(joint).valid(), "duplicate joint identity refused");
        const size_t clip_at = 32 + joints * 112;
        const size_t first_key = clip_at + 24 + 8;
        std::vector<uint8_t> nan_key = bytes;
        put_word(nan_key, first_key + 4, 0x7fc00000u);
        reseal(nan_key);
        expect(!anim::anim_v1::load(nan_key).valid(), "non-finite animation key refused");
        std::vector<uint8_t> zero_scale = bytes;
        put_word(zero_scale, first_key + 4 + 7 * 4, 0);
        reseal(zero_scale);
        expect(!anim::anim_v1::load(zero_scale).valid(), "zero animation scale refused");
        size_t event_at = clip_at + 24;
        for (size_t joint_index = 0; joint_index < joints; ++joint_index) {
            uint32_t keys = 0;
            anim::anim_v1::detail::word(bytes, event_at + 4, keys);
            event_at += 8 + size_t(keys) * anim::anim_v1::kKeyBytes;
        }
        uint32_t events = 0, duration_ticks = 0;
        anim::anim_v1::detail::word(bytes, clip_at + 8, duration_ticks);
        anim::anim_v1::detail::word(bytes, clip_at + 20, events);
        expect(events >= 2, "guide animation supplies ordered event pairs");
        if (events >= 2) {
            std::vector<uint8_t> invalid_event = bytes;
            put_word(invalid_event, event_at, 0);
            reseal(invalid_event);
            expect(!anim::anim_v1::load(invalid_event).valid(), "zero animation event id refused");
            std::vector<uint8_t> invalid_event_tick = bytes;
            put_word(invalid_event_tick, event_at + 4, duration_ticks + 1);
            reseal(invalid_event_tick);
            expect(!anim::anim_v1::load(invalid_event_tick).valid(), "out-of-range animation event refused");
            std::vector<uint8_t> unordered_events = bytes;
            put_word(unordered_events, event_at + 4, duration_ticks);
            put_word(unordered_events, event_at + anim::anim_v1::kEventBytes + 4, 0);
            reseal(unordered_events);
            expect(!anim::anim_v1::load(unordered_events).valid(), "out-of-order animation events refused");
        }
        std::vector<uint8_t> duplicate_clip = bytes;
        duplicate_clip.insert(duplicate_clip.end(), bytes.begin() + ptrdiff_t(clip_at), bytes.end());
        put_word(duplicate_clip, 8, uint32_t(duplicate_clip.size()));
        put_word(duplicate_clip, 24, 2);
        reseal(duplicate_clip);
        expect(!anim::anim_v1::load(duplicate_clip).valid(), "duplicate clip identity refused");
    }

    // Stress: many characters, independent clips/times, zero allocations per tick.
    Synthetic synthetic = synthetic_rig();
    expect(synthetic.rig.valid() && synthetic.clips.size() == 3 && synthetic.clips[2].valid(),
        "synthetic 64-joint rig and clips build");
    constexpr size_t guide_characters = 192, big_characters = 64, ticks = 300;
    std::vector<anim::OzzPoseContext> guide_contexts(guide_characters), big_contexts(big_characters);
    std::vector<float> guide_poses(guide_characters * joints * 10);
    std::vector<float> big_poses(big_characters * synthetic.rig.joint_count() * 10);
    for (auto& context : guide_contexts) context.prepare(package.rig, int(joints));
    for (auto& context : big_contexts) context.prepare(synthetic.rig, 64);
    std::vector<float> times(guide_characters + big_characters);
    for (size_t i = 0; i < times.size(); ++i) times[i] = 0.013f * float(i);

    const uint64_t new_before = g_new_calls.load();
    const uint64_t ozz_before = g_ozz_calls.load();
    bool all_ran = true;
    size_t samples = 0;
    for (size_t tick = 0; tick < ticks; ++tick) {
        for (size_t i = 0; i < guide_characters; ++i) {
            const float duration = package.clips[0].duration_seconds;
            times[i] = std::fmod(times[i] + (1.0f / 60.0f) * (0.5f + 0.01f * float(i % 50)), duration);
            all_ran = guide_contexts[i].sample(package.rig, package.clips[0].ozz, times[i],
                guide_poses.data() + i * joints * 10) && all_ran;
            ++samples;
        }
        for (size_t i = 0; i < big_characters; ++i) {
            const anim::OzzClip& clip = synthetic.clips[i % 3];
            float& t = times[guide_characters + i];
            t = std::fmod(t + (1.0f / 60.0f) * (0.75f + 0.02f * float(i % 25)), clip.duration_seconds);
            all_ran = big_contexts[i].sample(synthetic.rig, clip, t,
                big_poses.data() + i * synthetic.rig.joint_count() * 10) && all_ran;
            ++samples;
        }
    }
    const uint64_t new_calls = g_new_calls.load() - new_before;
    const uint64_t ozz_calls = g_ozz_calls.load() - ozz_before;
    std::printf("ozz service stress: %zu characters x %zu ticks = %zu samples, operator new %llu, ozz allocator %llu\n",
        guide_characters + big_characters, ticks, samples,
        (unsigned long long)new_calls, (unsigned long long)ozz_calls);
    expect(all_ran, "every stress sample ran");
    expect(new_calls == 0 && ozz_calls == 0, "sampling performs no heap allocation per tick");

    // Independence: two characters on one clip at different times differ, and
    // resampling a character reproduces its own pose regardless of others.
    {
        std::vector<float> a(joints * 10), b(joints * 10), again(joints * 10);
        guide_contexts[0].sample(package.rig, package.clips[0].ozz, 0.1f, a.data());
        guide_contexts[1].sample(package.rig, package.clips[0].ozz, 0.7f, b.data());
        guide_contexts[0].sample(package.rig, package.clips[0].ozz, 0.1f, again.data());
        expect(a != b, "different times give different poses");
        expect(a == again, "a context resamples deterministically after others ran");
    }

    // Allocation control: preparing a fresh context must be seen by the counters.
    {
        const uint64_t before = g_new_calls.load() + g_ozz_calls.load();
        anim::OzzPoseContext fresh;
        fresh.prepare(synthetic.rig, 64);
        expect(g_new_calls.load() + g_ozz_calls.load() > before, "allocation counters observe context preparation");
    }
    return g_failures == 0 ? 0 : 1;
}
