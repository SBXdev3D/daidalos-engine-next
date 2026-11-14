// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "ShaderCompiler.hpp"

using namespace daidalos;
using renderer::CompileShader;
using renderer::ShaderStage;
using renderer::ShaderWatcher;

namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203;

class ShaderFiles : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_directory = std::filesystem::temp_directory_path() / "daidalos-shader-tests" / info->name();
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
    }

    void TearDown() override { std::filesystem::remove_all(m_directory); }

    std::filesystem::path Write(const std::string& name, const std::string& text) {
        const std::filesystem::path path = m_directory / name;
        std::ofstream(path) << text;
        return path;
    }

    std::filesystem::path m_directory;
};

const std::string kValidShader = R"(
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }
)";

} // namespace

TEST_F(ShaderFiles, ValidShaderCompilesToSpirv) {
    const std::filesystem::path source = Write("Valid.slang", kValidShader);
    for (const ShaderStage stage : {ShaderStage::Vertex, ShaderStage::Pixel}) {
        const auto words = CompileShader(DAI_SLANGC_PATH, source, stage);
        ASSERT_TRUE(words.has_value()) << core::Format(words.error());
        ASSERT_FALSE(words->empty());
        EXPECT_EQ(words->front(), kSpirvMagic);
    }
}

TEST_F(ShaderFiles, ErrorNamesTheFileAndLine) {
    const std::filesystem::path source =
        Write("Broken.slang", "float4 VSMain() : SV_Position {\n    return nope;\n}\n");
    const auto words = CompileShader(DAI_SLANGC_PATH, source, ShaderStage::Vertex);
    ASSERT_FALSE(words.has_value());
    EXPECT_EQ(words.error().code, core::ErrorCode::ParseError);
    EXPECT_NE(words.error().message.find("Broken.slang:2"), std::string::npos) << words.error().message;
}

TEST_F(ShaderFiles, MissingCompilerIsAnIoError) {
    const std::filesystem::path source = Write("Valid.slang", kValidShader);
    const auto words = CompileShader(m_directory / "no-such-slangc", source, ShaderStage::Vertex);
    ASSERT_FALSE(words.has_value());
    EXPECT_EQ(words.error().code, core::ErrorCode::IoError);
}

TEST_F(ShaderFiles, WatcherSeesNothingWithoutEdits) {
    Write("Mesh.slang", kValidShader);
    ShaderWatcher watcher(m_directory);
    EXPECT_FALSE(watcher.Poll());
}

TEST_F(ShaderFiles, WatcherSeesAnEditOnce) {
    const std::filesystem::path source = Write("Mesh.slang", kValidShader);
    ShaderWatcher watcher(m_directory);

    std::filesystem::last_write_time(source, std::filesystem::last_write_time(source) + std::chrono::seconds(5));
    EXPECT_TRUE(watcher.Poll());
    EXPECT_FALSE(watcher.Poll());
}

TEST_F(ShaderFiles, WatcherSeesAddedAndRemovedFiles) {
    ShaderWatcher watcher(m_directory);
    const std::filesystem::path added = Write("Common.slangh", "");
    EXPECT_TRUE(watcher.Poll());
    std::filesystem::remove(added);
    EXPECT_TRUE(watcher.Poll());
}

TEST_F(ShaderFiles, WatcherIgnoresOtherFiles) {
    ShaderWatcher watcher(m_directory);
    Write("Mesh.slang.tmp", "");
    EXPECT_FALSE(watcher.Poll());
}

TEST(ShaderWatcher, MissingDirectoryReportsNothing) {
    ShaderWatcher watcher("daidalos-no-such-directory");
    EXPECT_FALSE(watcher.Poll());
}
