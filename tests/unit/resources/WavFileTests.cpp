// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/resources/WavFile.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

using namespace daidalos;

namespace {

template <typename T>
T ReadAt(const std::vector<std::byte>& bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::string_view TagAt(const std::vector<std::byte>& bytes, std::size_t offset) {
    return {reinterpret_cast<const char*>(bytes.data()) + offset, 4};
}

} // namespace

TEST(WavFileTest, AWavHasItsHeaderAndClippedSixteenBitSamples) {
    const std::vector<float> samples{0.0f, 1.0f, -1.0f, 0.5f, 2.0f, -3.0f};
    const core::Result<std::vector<std::byte>> wav = resources::EncodeWav(48000, 2, samples);
    ASSERT_TRUE(wav.has_value()) << core::Format(wav.error());
    ASSERT_EQ(wav->size(), 44U + (6 * 2));
    EXPECT_EQ(TagAt(*wav, 0), "RIFF");
    EXPECT_EQ(ReadAt<std::uint32_t>(*wav, 4), 36U + 12U);
    EXPECT_EQ(TagAt(*wav, 8), "WAVE");
    EXPECT_EQ(TagAt(*wav, 12), "fmt ");
    EXPECT_EQ(ReadAt<std::uint16_t>(*wav, 20), 1U);
    EXPECT_EQ(ReadAt<std::uint16_t>(*wav, 22), 2U);
    EXPECT_EQ(ReadAt<std::uint32_t>(*wav, 24), 48000U);
    EXPECT_EQ(ReadAt<std::uint32_t>(*wav, 28), 48000U * 4);
    EXPECT_EQ(ReadAt<std::uint16_t>(*wav, 32), 4U);
    EXPECT_EQ(ReadAt<std::uint16_t>(*wav, 34), 16U);
    EXPECT_EQ(TagAt(*wav, 36), "data");
    EXPECT_EQ(ReadAt<std::uint32_t>(*wav, 40), 12U);
    EXPECT_EQ(ReadAt<std::int16_t>(*wav, 44), 0);
    EXPECT_EQ(ReadAt<std::int16_t>(*wav, 46), 32767);
    EXPECT_EQ(ReadAt<std::int16_t>(*wav, 48), -32767);
    EXPECT_EQ(ReadAt<std::int16_t>(*wav, 50), 16384);
    EXPECT_EQ(ReadAt<std::int16_t>(*wav, 52), 32767);
    EXPECT_EQ(ReadAt<std::int16_t>(*wav, 54), -32767);
}

TEST(WavFileTest, SamplesMustFillEveryChannel) {
    const std::vector<float> samples{0.0f, 0.0f, 0.0f};
    EXPECT_FALSE(resources::EncodeWav(48000, 2, samples).has_value());
    EXPECT_FALSE(resources::EncodeWav(0, 1, samples).has_value());
}
