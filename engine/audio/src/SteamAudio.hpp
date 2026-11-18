// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

// Steam Audio behind the audio module (ADR-0065): the head-related transfer function, and the direct path of
// each sound through the level's geometry, its occlusion and transmission. Nothing outside src/ sees it.

#include <daidalos/audio/Audio.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <miniaudio.h>
#include <phonon.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace daidalos::audio::steam {

/// Samples per block that the effects process, and the voices' latency.
inline constexpr std::uint32_t kFrameSize = 256;

/// What a whole engine shares: Steam Audio's context and the default HRTF.
class Context {
public:
    /// Null if Steam Audio cannot start: sounds then use miniaudio's own panning.
    static std::unique_ptr<Context> Create(std::uint32_t sampleRate);
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    [[nodiscard]] IPLContext Get() const { return m_context; }
    [[nodiscard]] IPLHRTF Hrtf() const { return m_hrtf; }
    [[nodiscard]] const IPLAudioSettings& Settings() const { return m_settings; }

private:
    Context() = default;
    IPLContext m_context = nullptr;
    IPLHRTF m_hrtf = nullptr;
    IPLAudioSettings m_settings{};
};

/// What the simulation found for a voice, which its node applies: read by the audio thread.
struct DirectParams {
    IPLVector3 direction{0.0f, 0.0f, -1.0f}; // Towards the source, in the listener's frame.
    float distanceAttenuation = 1.0f;
    std::array<float, 3> airAbsorption{1.0f, 1.0f, 1.0f};
    float occlusion = 1.0f;
    std::array<float, 3> transmission{1.0f, 1.0f, 1.0f};
};

/// A miniaudio node after a voice: Steam Audio's direct effect, then its binaural effect, in blocks of
/// kFrameSize, with that much latency. Stereo in, stereo out; the input is mixed to mono first.
class SpatialNode {
public:
    /// Null if it cannot be made. The node outputs to target.
    static std::unique_ptr<SpatialNode> Create(const Context& context, ma_engine* engine, ma_node* target);
    ~SpatialNode();
    SpatialNode(const SpatialNode&) = delete;
    SpatialNode& operator=(const SpatialNode&) = delete;

    [[nodiscard]] ma_node* Node() { return &m_node.base; }
    /// From the game's thread.
    void SetParams(const DirectParams& params);
    [[nodiscard]] DirectParams Params();

private:
    explicit SpatialNode(const Context& context) : m_context(&context) {}
    static void Process(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut,
                        ma_uint32* frameCountOut);
    void ProcessBlock();

    struct NodeBase {
        ma_node_base base;
        SpatialNode* owner;
    };
    const Context* m_context;
    NodeBase m_node{};
    bool m_nodeReady = false;
    IPLDirectEffect m_direct = nullptr;
    IPLBinauralEffect m_binaural = nullptr;

    std::mutex m_paramsMutex;
    DirectParams m_params;
    DirectParams m_audioParams; // The audio thread's copy.

    // Mono in, interleaved stereo out, each a ring of frames.
    static constexpr std::uint32_t kRing = 16384;
    std::vector<float> m_in = std::vector<float>(kRing);
    std::uint32_t m_inRead = 0;
    std::uint32_t m_inCount = 0;
    std::vector<float> m_out = std::vector<float>(std::size_t{kRing} * 2);
    std::uint32_t m_outRead = 0;
    std::uint32_t m_outCount = 0;
    std::array<float, kFrameSize> m_block{};
    std::array<float, kFrameSize> m_directed{};
    std::array<float, kFrameSize> m_left{};
    std::array<float, kFrameSize> m_right{};
};

/// A world's acoustic geometry and the simulation of each voice's direct path through it.
class Scene {
public:
    static std::unique_ptr<Scene> Create(const Context& context);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    /// A mesh that never moves, already in world space.
    void AddStatic(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                   const AcousticMaterial& material);
    /// A mesh that moves, as doors do: its index, for Move.
    std::size_t AddMovable(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                           const AcousticMaterial& material, const glm::mat4& transform);
    void Move(std::size_t movable, const glm::mat4& transform);

    /// A source the simulation follows. Release it with RemoveSource.
    IPLSource AddSource();
    void RemoveSource(IPLSource source);

    struct Listener {
        glm::vec3 position{0.0f};
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
    };
    struct Voice {
        IPLSource source;
        glm::vec3 position;
        float minDistance;
        float maxDistance;
        SpatialNode* node;
    };
    /// Commits what changed, simulates each voice's direct path and hands the result to its node.
    void Simulate(const Listener& listener, std::span<const Voice> voices);
    /// A voice that moved between simulations: its direction and distance now, its occlusion as last found.
    void Place(const Listener& listener, const Voice& voice) const;

    [[nodiscard]] std::size_t TriangleCount() const { return m_triangles; }

private:
    explicit Scene(const Context& context) : m_context(&context) {}
    [[nodiscard]] DirectParams Placed(const Listener& listener, const Voice& voice) const;
    IPLStaticMesh MakeMesh(IPLScene scene, std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                           const AcousticMaterial& material);

    struct Movable {
        IPLScene subScene = nullptr;
        IPLStaticMesh mesh = nullptr;
        IPLInstancedMesh instance = nullptr;
    };
    const Context* m_context;
    IPLScene m_scene = nullptr;
    IPLSimulator m_simulator = nullptr;
    std::vector<IPLStaticMesh> m_static;
    std::vector<Movable> m_movable;
    bool m_dirty = true;
    std::size_t m_triangles = 0;
};

} // namespace daidalos::audio::steam
