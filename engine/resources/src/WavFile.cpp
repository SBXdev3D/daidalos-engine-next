// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/resources/WavFile.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <string_view>

namespace daidalos::resources {

namespace {

constexpr std::string_view kModule = "resources";

template <typename T>
void Put(std::vector<std::byte>& out, T value) {
    static_assert(std::endian::native == std::endian::little, "WAV writing assumes a little-endian machine");
    std::array<std::byte, sizeof(T)> raw{};
    std::memcpy(raw.data(), &value, sizeof(T));
    out.insert(out.end(), raw.begin(), raw.end());
}

void Tag(std::vector<std::byte>& out, std::string_view tag) {
    for (const char letter : tag) {
        out.push_back(static_cast<std::byte>(letter));
    }
}

} // namespace

core::Result<std::vector<std::byte>> EncodeWav(std::uint32_t sampleRate, std::uint32_t channels,
                                               std::span<const float> samples) {
    if (sampleRate == 0 || channels == 0 || channels > 8 || samples.size() % channels != 0) {
        return core::MakeError(
            core::ErrorCode::InvalidArgument, kModule,
            std::format("Cannot write {} samples of {} channels at {} Hz", samples.size(), channels, sampleRate));
    }
    constexpr std::uint32_t kBytesPerSample = 2;
    const std::uint64_t dataBytes = std::uint64_t{samples.size()} * kBytesPerSample;
    if (dataBytes > std::numeric_limits<std::uint32_t>::max() - 36U) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule, "Too much sound for a WAV file");
    }
    std::vector<std::byte> wav;
    wav.reserve(44 + dataBytes);
    Tag(wav, "RIFF");
    Put(wav, static_cast<std::uint32_t>(36 + dataBytes));
    Tag(wav, "WAVE");
    Tag(wav, "fmt ");
    Put(wav, std::uint32_t{16});
    Put(wav, std::uint16_t{1}); // PCM.
    Put(wav, static_cast<std::uint16_t>(channels));
    Put(wav, sampleRate);
    Put(wav, sampleRate * channels * kBytesPerSample);
    Put(wav, static_cast<std::uint16_t>(channels * kBytesPerSample));
    Put(wav, static_cast<std::uint16_t>(kBytesPerSample * 8));
    Tag(wav, "data");
    Put(wav, static_cast<std::uint32_t>(dataBytes));
    for (const float sample : samples) {
        const float clipped = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
        Put(wav, static_cast<std::int16_t>(std::lround(clipped * 32767.0f)));
    }
    return wav;
}

} // namespace daidalos::resources
