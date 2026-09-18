#pragma once
// Wicked-free support shared by the native probe and the sanitizer boundary
// harness: the check helper, manifest parsing, cell-list parsing, and a
// generated test WAV. Nothing here includes a renderer header, so a harness
// that only exercises the untrusted libraries links without Wicked.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace probe {
bool check_named(const char* probe_name, bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "%s probe failed: %s\n", probe_name, message);
        return false;
    }
    return true;
}

bool check(bool value, const char* message) {
    return check_named("wicked", value, message);
}

bool load_manifest(const char* filename, std::map<std::string, std::string>& values) {
    std::ifstream input(filename);
    if (!input) {
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            return false;
        }
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return true;
}

// "x,y;x,y;..." grid cells from the Elisa maze topology. Empty when the
// manifest has no wall line, which keeps single-cube hosts working.
std::vector<std::pair<int, int>> parse_walls(const std::string& spec) {
    std::vector<std::pair<int, int>> cells;
    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(';', start);
        if (end == std::string::npos) {
            end = spec.size();
        }
        const std::string entry = spec.substr(start, end - start);
        const auto comma = entry.find(',');
        if (comma == std::string::npos) {
            return {};
        }
        cells.emplace_back(std::stoi(entry.substr(0, comma)), std::stoi(entry.substr(comma + 1)));
        start = end + 1;
    }
    return cells;
}

// A tiny PCM WAV in memory: 16-bit mono at 8 kHz for 50 ms. No asset file is
// needed to prove the audio path decodes bytes and reports real sample
// information, which is stronger evidence than "a play call returned".
std::vector<uint8_t> make_test_wav(int sample_count, int sample_rate) {
    const int data_bytes = sample_count * 2;
    const int file_bytes = 44 + data_bytes;
    std::vector<uint8_t> wav(file_bytes, 0);
    auto put16 = [&wav](int offset, int value) {
        wav[offset] = (uint8_t)(value & 0xFF);
        wav[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    };
    auto put32 = [&wav](int offset, int value) {
        wav[offset] = (uint8_t)(value & 0xFF);
        wav[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
        wav[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
        wav[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
    };
    wav[0] = 'R'; wav[1] = 'I'; wav[2] = 'F'; wav[3] = 'F';
    put32(4, file_bytes - 8);
    wav[8] = 'W'; wav[9] = 'A'; wav[10] = 'V'; wav[11] = 'E';
    wav[12] = 'f'; wav[13] = 'm'; wav[14] = 't'; wav[15] = ' ';
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, sample_rate);
    put32(28, sample_rate * 2);
    put16(32, 2);
    put16(34, 16);
    wav[36] = 'd'; wav[37] = 'a'; wav[38] = 't'; wav[39] = 'a';
    put32(40, data_bytes);
    for (int i = 0; i < sample_count; ++i) {
        const int sample = (i % 32 < 16) ? 4000 : -4000;
        put16(44 + i * 2, sample & 0xFFFF);
    }
    return wav;
}
}
