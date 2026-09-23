#pragma once

// Small streaming SHA-256 reader for bounded native asset verification.
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace elisa::assets {

class Sha256 {
public:
    void update(const uint8_t* bytes, size_t size) {
        total_bytes_ += size;
        for (size_t index = 0; index < size; ++index) {
            block_[block_size_++] = bytes[index];
            if (block_size_ == block_.size()) {
                transform();
                block_size_ = 0;
            }
        }
    }

    std::string finish() {
        const uint64_t bit_count = total_bytes_ * 8;
        block_[block_size_++] = 0x80;
        if (block_size_ > 56) {
            while (block_size_ < 64) block_[block_size_++] = 0;
            transform();
            block_size_ = 0;
        }
        while (block_size_ < 56) block_[block_size_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8) {
            block_[block_size_++] = uint8_t(bit_count >> shift);
        }
        transform();
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (uint32_t word : state_) output << std::setw(8) << word;
        return output.str();
    }

private:
    std::array<uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> block_{};
    size_t block_size_ = 0;
    uint64_t total_bytes_ = 0;

    static uint32_t rotate(uint32_t value, uint32_t bits) {
        return (value >> bits) | (value << (32 - bits));
    }

    void transform() {
        static constexpr std::array<uint32_t, 64> constants = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::array<uint32_t, 64> words{};
        for (size_t index = 0; index < 16; ++index) {
            const size_t offset = index * 4;
            words[index] = (uint32_t(block_[offset]) << 24) | (uint32_t(block_[offset + 1]) << 16) |
                (uint32_t(block_[offset + 2]) << 8) | uint32_t(block_[offset + 3]);
        }
        for (size_t index = 16; index < words.size(); ++index) {
            const uint32_t x = words[index - 15], y = words[index - 2];
            const uint32_t s0 = rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3);
            const uint32_t s1 = rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (size_t index = 0; index < words.size(); ++index) {
            const uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t first = h + s1 + choice + constants[index] + words[index];
            const uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t second = s0 + majority;
            h = g; g = f; f = e; e = d + first;
            d = c; c = b; b = a; a = first + second;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }
};

inline bool sha256_file(const std::filesystem::path& path, uint64_t expected_bytes,
        std::string& digest, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { error = "SHA-256 file could not be opened"; return false; }
    const std::streampos end = input.tellg();
    if (end < 0 || static_cast<uint64_t>(end) != expected_bytes) {
        error = "SHA-256 file size mismatch";
        return false;
    }
    input.seekg(0);
    Sha256 hash;
    std::array<uint8_t, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) hash.update(buffer.data(), static_cast<size_t>(count));
    }
    if (!input.eof()) { error = "SHA-256 file read failed"; return false; }
    digest = hash.finish();
    return true;
}

} // namespace elisa::assets
