// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/debugui/DebugUi.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <shaders/DebugUi.ps.spv.h>
#include <shaders/DebugUi.vs.spv.h>

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace daidalos::debugui {

namespace {

// Matches PushConstants in shaders/DebugUi.hlsl.
struct PushConstants {
    glm::vec2 scale;
    glm::vec2 translate;
    std::uint32_t texture;
};

// ImGui treats texture id 0 as invalid, but 0 is a valid bindless index.
ImTextureID ToTextureId(std::uint32_t bindlessIndex) {
    return static_cast<ImTextureID>(bindlessIndex) + 1;
}

std::uint32_t ToBindlessIndex(ImTextureID id) {
    return static_cast<std::uint32_t>(id - 1);
}

ImGuiKey ToImGuiKey(platform::Key key) {
    using platform::Key;
    const auto value = static_cast<int>(key);
    if (key >= Key::A && key <= Key::Z) {
        return static_cast<ImGuiKey>(ImGuiKey_A + (value - static_cast<int>(Key::A)));
    }
    if (key >= Key::Digit1 && key <= Key::Digit9) {
        return static_cast<ImGuiKey>(ImGuiKey_1 + (value - static_cast<int>(Key::Digit1)));
    }
    if (key >= Key::F1 && key <= Key::F12) {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (value - static_cast<int>(Key::F1)));
    }
    switch (key) {
    case Key::Digit0:
        return ImGuiKey_0;
    case Key::Enter:
        return ImGuiKey_Enter;
    case Key::Escape:
        return ImGuiKey_Escape;
    case Key::Backspace:
        return ImGuiKey_Backspace;
    case Key::Tab:
        return ImGuiKey_Tab;
    case Key::Space:
        return ImGuiKey_Space;
    case Key::Delete:
        return ImGuiKey_Delete;
    case Key::Home:
        return ImGuiKey_Home;
    case Key::End:
        return ImGuiKey_End;
    case Key::PageUp:
        return ImGuiKey_PageUp;
    case Key::PageDown:
        return ImGuiKey_PageDown;
    case Key::Left:
        return ImGuiKey_LeftArrow;
    case Key::Right:
        return ImGuiKey_RightArrow;
    case Key::Up:
        return ImGuiKey_UpArrow;
    case Key::Down:
        return ImGuiKey_DownArrow;
    case Key::LeftCtrl:
        return ImGuiKey_LeftCtrl;
    case Key::RightCtrl:
        return ImGuiKey_RightCtrl;
    case Key::LeftShift:
        return ImGuiKey_LeftShift;
    case Key::RightShift:
        return ImGuiKey_RightShift;
    case Key::LeftAlt:
        return ImGuiKey_LeftAlt;
    case Key::RightAlt:
        return ImGuiKey_RightAlt;
    default:
        return ImGuiKey_None;
    }
}

int ToImGuiButton(platform::MouseButton button) {
    switch (button) {
    case platform::MouseButton::Left:
        return ImGuiMouseButton_Left;
    case platform::MouseButton::Right:
        return ImGuiMouseButton_Right;
    case platform::MouseButton::Middle:
        return ImGuiMouseButton_Middle;
    default:
        return -1;
    }
}

void UpdateModifiers(ImGuiIO& io, platform::Key key, bool down) {
    using platform::Key;
    if (key == Key::LeftCtrl || key == Key::RightCtrl) {
        io.AddKeyEvent(ImGuiMod_Ctrl, down);
    } else if (key == Key::LeftShift || key == Key::RightShift) {
        io.AddKeyEvent(ImGuiMod_Shift, down);
    } else if (key == Key::LeftAlt || key == Key::RightAlt) {
        io.AddKeyEvent(ImGuiMod_Alt, down);
    }
}

} // namespace

struct DebugUi::Impl {
    struct GeometryBuffers {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        std::size_t vertexCapacity = 0;
        std::size_t indexCapacity = 0;
    };

    explicit Impl(rhi::Device& targetDevice) : device(&targetDevice), context(ImGui::CreateContext()) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        for (const GeometryBuffers& geometry : frames) {
            device->DestroyBuffer(geometry.vertices);
            device->DestroyBuffer(geometry.indices);
        }
        device->DestroyPipeline(pipeline);
        device->DestroyTexture(font);
        ImGui::DestroyContext(context);
    }

    core::Result<void> Reserve(GeometryBuffers& geometry, std::size_t vertices, std::size_t indices) const;

    rhi::Device* device;
    ImGuiContext* context;
    rhi::PipelineHandle pipeline;
    rhi::TextureHandle font;
    std::array<GeometryBuffers, rhi::kFramesInFlight> frames;
    std::uint32_t frameIndex = 0;
};

core::Result<void> DebugUi::Impl::Reserve(GeometryBuffers& geometry, std::size_t vertices, std::size_t indices) const {
    // The old buffers may be in use by the GPU: their destruction is deferred.
    if (vertices > geometry.vertexCapacity) {
        const std::size_t capacity = std::max(vertices * 2, std::size_t{4096});
        device->DestroyBuffer(geometry.vertices);
        geometry.vertices = {};
        geometry.vertexCapacity = 0;
        DAI_TRY_ASSIGN(rhi::BufferHandle buffer, device->CreateBuffer({.size = capacity * sizeof(ImDrawVert),
                                                                       .usage = rhi::BufferUsage::Vertex,
                                                                       .memory = rhi::MemoryLocation::CpuToGpu,
                                                                       .name = "DebugUi vertices"}));
        geometry.vertices = buffer;
        geometry.vertexCapacity = capacity;
    }
    if (indices > geometry.indexCapacity) {
        const std::size_t capacity = std::max(indices * 2, std::size_t{8192});
        device->DestroyBuffer(geometry.indices);
        geometry.indices = {};
        geometry.indexCapacity = 0;
        DAI_TRY_ASSIGN(rhi::BufferHandle buffer, device->CreateBuffer({.size = capacity * sizeof(ImDrawIdx),
                                                                       .usage = rhi::BufferUsage::Index,
                                                                       .memory = rhi::MemoryLocation::CpuToGpu,
                                                                       .name = "DebugUi indices"}));
        geometry.indices = buffer;
        geometry.indexCapacity = capacity;
    }
    return {};
}

core::Result<DebugUi> DebugUi::Create(rhi::Device& device, rhi::Format colorFormat) {
    DAI_ASSERT(ImGui::GetCurrentContext() == nullptr, "Only one DebugUi can exist at a time");

    auto impl = std::make_unique<Impl>(device);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Window positions stay out of the working directory.
    io.BackendRendererName = "daidalos";
    io.BackendPlatformName = "daidalos";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    DAI_TRY_ASSIGN(
        rhi::TextureHandle font,
        device.CreateTexture({.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)},
                              .format = rhi::Format::Rgba8Unorm,
                              .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                              .name = "DebugUi font"}));
    impl->font = font;
    const auto byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    DAI_TRY(device.UploadTexture(font, std::as_bytes(std::span(pixels, byteCount))));
    io.Fonts->SetTexID(ToTextureId(device.BindlessIndex(font)));

    const std::vector<std::uint32_t> vertexShader = rhi::ToSpirv(kDebugUiVsSpirv);
    const std::vector<std::uint32_t> fragmentShader = rhi::ToSpirv(kDebugUiPsSpirv);
    DAI_TRY_ASSIGN(
        rhi::PipelineHandle pipeline,
        device.CreatePipeline({
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .vertexStride = sizeof(ImDrawVert),
            .vertexAttributes = {{0, rhi::Format::Rg32Float, static_cast<std::uint32_t>(offsetof(ImDrawVert, pos))},
                                 {1, rhi::Format::Rg32Float, static_cast<std::uint32_t>(offsetof(ImDrawVert, uv))},
                                 {2, rhi::Format::Rgba8Unorm, static_cast<std::uint32_t>(offsetof(ImDrawVert, col))}},
            .colorFormat = colorFormat,
            .depthTest = false,
            .depthWrite = false,
            .cull = rhi::CullMode::None,
            .blend = rhi::BlendMode::Alpha,
            .name = "DebugUi",
        }));
    impl->pipeline = pipeline;
    return DebugUi(std::move(impl));
}

DebugUi::DebugUi(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
DebugUi::DebugUi(DebugUi&& other) noexcept = default;
DebugUi& DebugUi::operator=(DebugUi&& other) noexcept = default;
DebugUi::~DebugUi() = default;

void DebugUi::ProcessEvent(const platform::Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    std::visit(
        [&io](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, platform::MouseMoved>) {
                io.AddMousePosEvent(value.position.x, value.position.y);
            } else if constexpr (std::is_same_v<T, platform::MouseButtonPressed>) {
                if (const int button = ToImGuiButton(value.button); button >= 0) {
                    io.AddMouseButtonEvent(button, true);
                }
            } else if constexpr (std::is_same_v<T, platform::MouseButtonReleased>) {
                if (const int button = ToImGuiButton(value.button); button >= 0) {
                    io.AddMouseButtonEvent(button, false);
                }
            } else if constexpr (std::is_same_v<T, platform::MouseWheelScrolled>) {
                io.AddMouseWheelEvent(value.delta.x, value.delta.y);
            } else if constexpr (std::is_same_v<T, platform::KeyPressed>) {
                UpdateModifiers(io, value.key, true);
                io.AddKeyEvent(ToImGuiKey(value.key), true);
            } else if constexpr (std::is_same_v<T, platform::KeyReleased>) {
                UpdateModifiers(io, value.key, false);
                io.AddKeyEvent(ToImGuiKey(value.key), false);
            } else if constexpr (std::is_same_v<T, platform::TextEntered>) {
                io.AddInputCharactersUTF8(value.text.c_str());
            } else if constexpr (std::is_same_v<T, platform::WindowFocusGained>) {
                io.AddFocusEvent(true);
            } else if constexpr (std::is_same_v<T, platform::WindowFocusLost>) {
                io.AddFocusEvent(false);
            }
        },
        event);
}

void DebugUi::BeginFrame(std::chrono::duration<double> delta, platform::Extent windowSize, platform::Extent pixelSize) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(windowSize.width), static_cast<float>(windowSize.height));
    if (windowSize.width != 0 && windowSize.height != 0) {
        io.DisplayFramebufferScale =
            ImVec2(static_cast<float>(pixelSize.width) / static_cast<float>(windowSize.width),
                   static_cast<float>(pixelSize.height) / static_cast<float>(windowSize.height));
    }
    // ImGui rejects a zero delta, which a paused frame timer can produce.
    io.DeltaTime = std::max(static_cast<float>(delta.count()), 1.0e-4f);
    ImGui::NewFrame();
}

void DebugUi::EndFrame() {
    ImGui::Render();
}

core::Result<void> DebugUi::Draw(rhi::CommandList& commands, rhi::Extent extent) {
    DAI_PROFILE_SCOPE("debugui::Draw");
    const ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr || drawData->TotalVtxCount == 0) {
        return {};
    }

    Impl::GeometryBuffers& geometry = m_impl->frames[m_impl->frameIndex];
    m_impl->frameIndex = (m_impl->frameIndex + 1) % rhi::kFramesInFlight;
    if (core::Result<void> reserved = m_impl->Reserve(geometry, static_cast<std::size_t>(drawData->TotalVtxCount),
                                                      static_cast<std::size_t>(drawData->TotalIdxCount));
        !reserved) {
        return reserved;
    }

    std::byte* vertices = m_impl->device->Mapped(geometry.vertices).data();
    std::byte* indices = m_impl->device->Mapped(geometry.indices).data();
    for (const ImDrawList* list : drawData->CmdLists) {
        const auto vertexBytes = static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const auto indexBytes = static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        std::memcpy(vertices, list->VtxBuffer.Data, vertexBytes);
        std::memcpy(indices, list->IdxBuffer.Data, indexBytes);
        vertices += vertexBytes;
        indices += indexBytes;
    }

    // Y goes down in ImGui and up in clip space (the viewport flips it once, CONVENTIONS.md section 5).
    const glm::vec2 position(drawData->DisplayPos.x, drawData->DisplayPos.y);
    const glm::vec2 size(drawData->DisplaySize.x, drawData->DisplaySize.y);
    const glm::vec2 scale(2.0f / size.x, -2.0f / size.y);
    const glm::vec2 translate = glm::vec2(-1.0f, 1.0f) - position * scale;

    commands.BindPipeline(m_impl->pipeline);
    commands.BindVertexBuffer(geometry.vertices);
    commands.BindIndexBuffer(geometry.indices,
                             sizeof(ImDrawIdx) == 2 ? rhi::IndexType::Uint16 : rhi::IndexType::Uint32);

    const glm::vec2 pixelScale(drawData->FramebufferScale.x, drawData->FramebufferScale.y);
    const glm::vec2 limit(extent.width, extent.height);
    std::uint32_t firstIndex = 0;
    std::int32_t firstVertex = 0;
    for (const ImDrawList* list : drawData->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            const glm::vec2 clipMin = glm::clamp(
                (glm::vec2(command.ClipRect.x, command.ClipRect.y) - position) * pixelScale, glm::vec2(0.0f), limit);
            const glm::vec2 clipMax = glm::clamp(
                (glm::vec2(command.ClipRect.z, command.ClipRect.w) - position) * pixelScale, glm::vec2(0.0f), limit);
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
                continue;
            }
            commands.SetScissor(static_cast<std::int32_t>(clipMin.x), static_cast<std::int32_t>(clipMin.y),
                                static_cast<std::uint32_t>(clipMax.x - clipMin.x),
                                static_cast<std::uint32_t>(clipMax.y - clipMin.y));
            commands.Push(PushConstants{scale, translate, ToBindlessIndex(command.GetTexID())});
            commands.DrawIndexed(command.ElemCount, firstIndex + command.IdxOffset,
                                 firstVertex + static_cast<std::int32_t>(command.VtxOffset));
        }
        firstIndex += static_cast<std::uint32_t>(list->IdxBuffer.Size);
        firstVertex += list->VtxBuffer.Size;
    }
    return {};
}

bool DebugUi::WantsMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUi::WantsKeyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace daidalos::debugui
