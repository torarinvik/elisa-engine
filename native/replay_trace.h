#pragma once

// Deterministic, bounded replay trace persistence. The file format is explicit
// little-endian data rather than a native struct dump, so traces remain valid
// across compiler and platform ABIs. Writes replace the destination through a
// temporary file; reads validate every byte before publishing a trace.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "probe_core.h"

namespace probe {

enum class ReplayTraceStatus {
    Ok,
    InvalidArgument,
    Capacity,
    NonMonotonicTick,
    InvalidHeader,
    InvalidVersion,
    InvalidScope,
    InvalidCount,
    InvalidFrame,
    IoError,
    Truncated,
    TrailingBytes,
    ChecksumMismatch,
};

inline const char* replay_trace_status_name(ReplayTraceStatus status) {
    switch (status) {
    case ReplayTraceStatus::Ok: return "ok";
    case ReplayTraceStatus::InvalidArgument: return "invalid argument";
    case ReplayTraceStatus::Capacity: return "capacity";
    case ReplayTraceStatus::NonMonotonicTick: return "non-monotonic tick";
    case ReplayTraceStatus::InvalidHeader: return "invalid header";
    case ReplayTraceStatus::InvalidVersion: return "invalid version";
    case ReplayTraceStatus::InvalidScope: return "invalid scope";
    case ReplayTraceStatus::InvalidCount: return "invalid count";
    case ReplayTraceStatus::InvalidFrame: return "invalid frame";
    case ReplayTraceStatus::IoError: return "I/O error";
    case ReplayTraceStatus::Truncated: return "truncated";
    case ReplayTraceStatus::TrailingBytes: return "trailing bytes";
    case ReplayTraceStatus::ChecksumMismatch: return "checksum mismatch";
    }
    return "unknown";
}

struct ReplayTraceFrame {
    int64_t tick = 0;
    int64_t input = 0;
    int64_t random_seed = 0;
    int64_t world_digest = 0;
    int64_t physics_digest = 0;
    int64_t render_digest = 0;
};

class ReplayTrace {
public:
    enum class Scope : uint8_t {
        SameBuild = 0,
        CrossBuild = 1,
    };

    static constexpr size_t kMaxFrames = 256;
    static constexpr uint32_t kVersion = 1;
    static constexpr size_t kHeaderBytes = 20;
    static constexpr size_t kFrameBytes = 48;
    static constexpr size_t kChecksumBytes = 8;
    static constexpr size_t kMaxFileBytes = kHeaderBytes + kMaxFrames * kFrameBytes + kChecksumBytes;

    explicit ReplayTrace(Scope scope = Scope::SameBuild) : scope_(scope) {}

    Scope scope() const { return scope_; }
    size_t frame_count() const { return count_; }
    const ReplayTraceFrame* frame_at(size_t index) const {
        return index < count_ ? &frames_[index] : nullptr;
    }

    ReplayTraceStatus append(const ReplayTraceFrame& frame) {
        if (frame.tick < 0) return ReplayTraceStatus::InvalidFrame;
        if (count_ >= kMaxFrames) return ReplayTraceStatus::Capacity;
        if (count_ != 0 && frame.tick <= frames_[count_ - 1].tick) {
            return ReplayTraceStatus::NonMonotonicTick;
        }
        frames_[count_++] = frame;
        return ReplayTraceStatus::Ok;
    }

    bool valid() const {
        if (count_ > kMaxFrames || !valid_scope(scope_)) return false;
        for (size_t index = 1; index < count_; ++index) {
            if (frames_[index].tick <= frames_[index - 1].tick) return false;
        }
        return true;
    }

    int64_t first_divergent_tick(const ReplayTrace& other) const {
        const size_t shared = count_ < other.count_ ? count_ : other.count_;
        for (size_t index = 0; index < shared; ++index) {
            if (!same_frame(frames_[index], other.frames_[index])) return frames_[index].tick;
        }
        if (count_ > shared) return frames_[shared].tick;
        if (other.count_ > shared) return other.frames_[shared].tick;
        return -1;
    }

    ReplayTraceStatus write(const std::filesystem::path& destination) const {
        if (destination.empty() || !valid()) return ReplayTraceStatus::InvalidArgument;
        const std::vector<uint8_t> bytes = encode();
        std::filesystem::path temporary = destination;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return ReplayTraceStatus::IoError;
            output.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return ReplayTraceStatus::IoError;
            }
        }
        std::error_code error;
        std::filesystem::rename(temporary, destination, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return ReplayTraceStatus::IoError;
        }
        return ReplayTraceStatus::Ok;
    }

    static ReplayTraceStatus read(const std::filesystem::path& source, ReplayTrace& output) {
        if (source.empty()) return ReplayTraceStatus::InvalidArgument;
        std::error_code error;
        const uintmax_t size = std::filesystem::file_size(source, error);
        if (error) return ReplayTraceStatus::IoError;
        if (size < kHeaderBytes + kChecksumBytes) return ReplayTraceStatus::Truncated;
        if (size > kMaxFileBytes) return ReplayTraceStatus::TrailingBytes;
        std::ifstream input(source, std::ios::binary);
        if (!input) return ReplayTraceStatus::IoError;
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input || static_cast<size_t>(input.gcount()) != bytes.size()) return ReplayTraceStatus::Truncated;
        return decode(bytes, output);
    }

private:
    static constexpr std::array<uint8_t, 8> kMagic = {'E', 'L', 'T', 'R', 'C', 'E', '0', '1'};

    static bool valid_scope(Scope scope) {
        return scope == Scope::SameBuild || scope == Scope::CrossBuild;
    }

    static bool same_frame(const ReplayTraceFrame& left, const ReplayTraceFrame& right) {
        return left.tick == right.tick && left.input == right.input &&
            left.random_seed == right.random_seed && left.world_digest == right.world_digest &&
            left.physics_digest == right.physics_digest && left.render_digest == right.render_digest;
    }

    static void put_u32(std::vector<uint8_t>& bytes, uint32_t value) {
        for (size_t shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
    }

    static void put_u64(std::vector<uint8_t>& bytes, uint64_t value) {
        for (size_t shift = 0; shift < 64; shift += 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
    }

    static uint32_t get_u32(const std::vector<uint8_t>& bytes, size_t offset) {
        uint32_t value = 0;
        for (size_t shift = 0; shift < 32; shift += 8) value |= static_cast<uint32_t>(bytes[offset + shift / 8]) << shift;
        return value;
    }

    static uint64_t get_u64(const std::vector<uint8_t>& bytes, size_t offset) {
        uint64_t value = 0;
        for (size_t shift = 0; shift < 64; shift += 8) value |= static_cast<uint64_t>(bytes[offset + shift / 8]) << shift;
        return value;
    }

    static uint64_t checksum(const std::vector<uint8_t>& bytes, size_t count) {
        uint64_t hash = 1469598103934665603ull;
        for (size_t index = 0; index < count; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static void append_frame(std::vector<uint8_t>& bytes, const ReplayTraceFrame& frame) {
        put_u64(bytes, static_cast<uint64_t>(frame.tick));
        put_u64(bytes, static_cast<uint64_t>(frame.input));
        put_u64(bytes, static_cast<uint64_t>(frame.random_seed));
        put_u64(bytes, static_cast<uint64_t>(frame.world_digest));
        put_u64(bytes, static_cast<uint64_t>(frame.physics_digest));
        put_u64(bytes, static_cast<uint64_t>(frame.render_digest));
    }

    std::vector<uint8_t> encode() const {
        std::vector<uint8_t> bytes;
        bytes.reserve(kHeaderBytes + count_ * kFrameBytes + kChecksumBytes);
        bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
        put_u32(bytes, kVersion);
        bytes.push_back(static_cast<uint8_t>(scope_));
        bytes.push_back(0);
        bytes.push_back(0);
        bytes.push_back(0);
        put_u32(bytes, static_cast<uint32_t>(count_));
        for (size_t index = 0; index < count_; ++index) append_frame(bytes, frames_[index]);
        put_u64(bytes, checksum(bytes, bytes.size()));
        return bytes;
    }

    static ReplayTraceStatus decode(const std::vector<uint8_t>& bytes, ReplayTrace& output) {
        if (bytes.size() < kHeaderBytes + kChecksumBytes) return ReplayTraceStatus::Truncated;
        for (size_t index = 0; index < kMagic.size(); ++index) {
            if (bytes[index] != kMagic[index]) return ReplayTraceStatus::InvalidHeader;
        }
        if (get_u32(bytes, 8) != kVersion) return ReplayTraceStatus::InvalidVersion;
        if (bytes[13] != 0 || bytes[14] != 0 || bytes[15] != 0) return ReplayTraceStatus::InvalidHeader;
        const uint8_t raw_scope = bytes[12];
        if (raw_scope > static_cast<uint8_t>(Scope::CrossBuild)) return ReplayTraceStatus::InvalidScope;
        const uint32_t count = get_u32(bytes, 16);
        if (count > kMaxFrames) return ReplayTraceStatus::InvalidCount;
        const size_t expected = kHeaderBytes + static_cast<size_t>(count) * kFrameBytes + kChecksumBytes;
        if (bytes.size() < expected) return ReplayTraceStatus::Truncated;
        if (bytes.size() > expected) return ReplayTraceStatus::TrailingBytes;
        if (get_u64(bytes, bytes.size() - kChecksumBytes) != checksum(bytes, bytes.size() - kChecksumBytes)) {
            return ReplayTraceStatus::ChecksumMismatch;
        }
        ReplayTrace candidate(static_cast<Scope>(raw_scope));
        size_t offset = kHeaderBytes;
        for (uint32_t index = 0; index < count; ++index) {
            ReplayTraceFrame frame{
                static_cast<int64_t>(get_u64(bytes, offset)),
                static_cast<int64_t>(get_u64(bytes, offset + 8)),
                static_cast<int64_t>(get_u64(bytes, offset + 16)),
                static_cast<int64_t>(get_u64(bytes, offset + 24)),
                static_cast<int64_t>(get_u64(bytes, offset + 32)),
                static_cast<int64_t>(get_u64(bytes, offset + 40)),
            };
            const ReplayTraceStatus status = candidate.append(frame);
            if (status != ReplayTraceStatus::Ok) return status == ReplayTraceStatus::NonMonotonicTick
                ? ReplayTraceStatus::InvalidFrame : status;
            offset += kFrameBytes;
        }
        output = candidate;
        return ReplayTraceStatus::Ok;
    }

    std::array<ReplayTraceFrame, kMaxFrames> frames_{};
    size_t count_ = 0;
    Scope scope_ = Scope::SameBuild;
};

inline bool probe_replay_trace() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "elisa-replay-trace.bin";
    ReplayTrace trace(ReplayTrace::Scope::SameBuild);
    const ReplayTraceFrame first{1, 3, 7, 11, 13, 17};
    const ReplayTraceFrame second{2, 4, 7, 19, 23, 29};
    if (!check(trace.append(first) == ReplayTraceStatus::Ok, "replay trace appends first frame") ||
        !check(trace.append(second) == ReplayTraceStatus::Ok, "replay trace appends ordered frame") ||
        !check(trace.append(second) == ReplayTraceStatus::NonMonotonicTick, "replay trace rejects duplicate tick") ||
        !check(trace.write(path) == ReplayTraceStatus::Ok, "replay trace writes atomically")) return false;

    ReplayTrace loaded(ReplayTrace::Scope::CrossBuild);
    if (!check(ReplayTrace::read(path, loaded) == ReplayTraceStatus::Ok, "replay trace reads and validates") ||
        !check(loaded.scope() == ReplayTrace::Scope::SameBuild && loaded.frame_count() == 2,
            "replay trace preserves scope and count") ||
        !check(loaded.frame_at(1) != nullptr && loaded.frame_at(1)->render_digest == 29,
            "replay trace preserves digest payload") ||
        !check(loaded.first_divergent_tick(trace) == -1, "replay trace round trip compares equal")) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return false;
    }

    ReplayTrace replacement(ReplayTrace::Scope::SameBuild);
    const ReplayTraceFrame third{3, 5, 7, 31, 37, 41};
    if (!check(replacement.append(first) == ReplayTraceStatus::Ok &&
            replacement.append(second) == ReplayTraceStatus::Ok &&
            replacement.append(third) == ReplayTraceStatus::Ok &&
            replacement.write(path) == ReplayTraceStatus::Ok,
        "replay trace replaces an existing destination")) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return false;
    }
    ReplayTrace replaced;
    if (!check(ReplayTrace::read(path, replaced) == ReplayTraceStatus::Ok &&
            replaced.frame_count() == 3 && replaced.frame_at(2)->input == 5,
        "replay trace publishes the replacement as a whole")) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return false;
    }

    std::filesystem::path corrupt = path;
    corrupt += ".corrupt";
    {
        std::ifstream input(path, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        bytes[bytes.size() - ReplayTrace::kChecksumBytes - 1] ^= 1;
        std::ofstream output(corrupt, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ReplayTrace ignored_trace;
    const bool corruption_rejected = ReplayTrace::read(corrupt, ignored_trace) == ReplayTraceStatus::ChecksumMismatch;
    std::filesystem::path truncated = path;
    truncated += ".truncated";
    {
        std::ifstream input(path, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        std::ofstream output(truncated, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), 10);
    }
    const bool truncation_rejected = ReplayTrace::read(truncated, ignored_trace) == ReplayTraceStatus::Truncated;
    std::filesystem::path trailing = path;
    trailing += ".trailing";
    {
        std::ifstream input(path, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        bytes.push_back(0);
        std::ofstream output(trailing, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const bool trailing_rejected = ReplayTrace::read(trailing, ignored_trace) == ReplayTraceStatus::TrailingBytes;
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(corrupt, ignored);
    std::filesystem::remove(truncated, ignored);
    std::filesystem::remove(trailing, ignored);
    return check(corruption_rejected, "replay trace rejects checksum corruption") &&
        check(truncation_rejected, "replay trace rejects truncation") &&
        check(trailing_rejected, "replay trace rejects trailing bytes");
}

} // namespace probe
