// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

// Steam Audio behind the audio module (ADR-0065, ADR-0066): the head-related transfer function, the direct path
// of each sound through the level's geometry, the paths it takes around it, and the reverb of the room the
// listener is in. Nothing outside src/ sees it.

#include <daidalos/audio/Audio.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <miniaudio.h>
#include <phonon.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
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

/// Where a sound comes from: what the simulation found for a voice, which its node applies.
struct DirectParams {
    IPLVector3 direction{0.0f, 0.0f, -1.0f}; // Towards the source, in the listener's frame.
    float distanceAttenuation = 1.0f;
    std::array<float, 3> airAbsorption{1.0f, 1.0f, 1.0f};
    float occlusion = 1.0f;
    std::array<float, 3> transmission{1.0f, 1.0f, 1.0f};
};

/// The way around (ADR-0066): first-order Ambisonics of the paths from the source to the listener, in world
/// space, and their EQ. Absent until pathing is baked.
struct PathParams {
    bool valid = false;
    std::array<float, 3> eq{1.0f, 1.0f, 1.0f};
    std::array<float, 4> sh{};
    IPLCoordinateSpace3 listener{};
};

/// The reverb of where the listener is (ADR-0066), from reflections traced around it.
struct ReverbParams {
    bool valid = false;
    std::array<float, 3> reverbTimes{0.0f, 0.0f, 0.0f};
};

/// A ring of interleaved frames that the audio thread fills and empties: what a node holds between the sizes
/// miniaudio asks for and the blocks Steam Audio works in.
class Ring {
public:
    explicit Ring(std::uint32_t channels) : m_channels(channels), m_data(std::size_t{kCapacity} * channels) {}
    [[nodiscard]] std::uint32_t Count() const { return m_count; }
    void Push(const float* frame);
    /// Fills frame with zeros when the ring is empty.
    void Pop(float* frame);
    void PushSilence(std::uint32_t frames);

private:
    static constexpr std::uint32_t kCapacity = 16384;
    std::uint32_t m_channels;
    std::vector<float> m_data;
    std::uint32_t m_read = 0;
    std::uint32_t m_count = 0;
};

class ReverbNode;

/// Where pathing's probes go (ADR-0066): on a grid a spacing apart, height above each triangle of floors (three
/// corners each).
[[nodiscard]] std::vector<glm::vec3> FloorProbes(std::span<const glm::vec3> floors, float spacing, float height);

/// A miniaudio node after a voice: the direct path through Steam Audio's direct effect and HRTF, plus the
/// path around what blocks it, in blocks of kFrameSize, with that much latency. Stereo in; out, the voice's
/// sound to its bus, and a send to the reverb.
class SpatialNode {
public:
    /// Null if it cannot be made. The node outputs to target, and sends to reverb, if any.
    static std::shared_ptr<SpatialNode> Create(const Context& context, ma_engine* engine, ma_node* target,
                                               ReverbNode* reverb);
    ~SpatialNode();
    SpatialNode(const SpatialNode&) = delete;
    SpatialNode& operator=(const SpatialNode&) = delete;

    [[nodiscard]] ma_node* Node() { return &m_node.base; }
    /// From the game's thread.
    void SetParams(const DirectParams& params);
    [[nodiscard]] DirectParams Params();
    /// From the simulation's thread.
    void SetPath(const PathParams& path);

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
    IPLPathEffect m_path = nullptr;

    std::mutex m_paramsMutex;
    DirectParams m_params;
    PathParams m_pathParams;
    // The audio thread's copies.
    DirectParams m_audioParams;
    PathParams m_audioPath;

    Ring m_in{1};
    Ring m_out{2};
    Ring m_send{1};
    std::array<float, kFrameSize> m_block{};
    std::array<float, kFrameSize> m_directed{};
    std::array<float, kFrameSize> m_left{};
    std::array<float, kFrameSize> m_right{};
    std::array<float, kFrameSize> m_pathLeft{};
    std::array<float, kFrameSize> m_pathRight{};
};

/// The world's reverb (ADR-0066): voices send to it, and it plays their sound through the reverb of the room
/// the listener is in, to the effects bus.
class ReverbNode {
public:
    static std::unique_ptr<ReverbNode> Create(const Context& context, ma_engine* engine, ma_node* target);
    ~ReverbNode();
    ReverbNode(const ReverbNode&) = delete;
    ReverbNode& operator=(const ReverbNode&) = delete;

    [[nodiscard]] ma_node* Node() { return &m_node.base; }
    void SetParams(const ReverbParams& params);

    /// The reverb's share of the sound, per unit of what voices send.
    static constexpr float kWet = 0.35f;
    /// Seconds of reverb tail at most.
    static constexpr float kDuration = 2.0f;

private:
    explicit ReverbNode(const Context& context) : m_context(&context) {}
    static void Process(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut,
                        ma_uint32* frameCountOut);
    void ProcessBlock();

    struct NodeBase {
        ma_node_base base;
        ReverbNode* owner;
    };
    const Context* m_context;
    NodeBase m_node{};
    bool m_nodeReady = false;
    IPLReflectionEffect m_effect = nullptr;
    std::mutex m_paramsMutex;
    ReverbParams m_params;
    ReverbParams m_audioParams;
    Ring m_in{1};
    Ring m_out{2};
    std::array<float, kFrameSize> m_block{};
    std::array<float, kFrameSize> m_wet{};
};

/// A world's acoustic geometry and its simulation: each voice's direct path on the game's thread, and on a
/// worker thread, the reverb around the listener and the paths around obstacles.
class Scene {
public:
    /// The reverb plays to target.
    static std::unique_ptr<Scene> Create(const Context& context, ma_engine* engine, ma_node* target);
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
    /// Places probes over the static geometry's floors, a spacing apart, and bakes the paths between them on
    /// the worker thread; voices take paths once it is done.
    void StartPathing(float spacing);
    [[nodiscard]] PathingState Pathing() const;

    /// A source the simulation follows, for a voice. Release it with RemoveSource.
    IPLSource AddSource();
    void RemoveSource(IPLSource source);

    [[nodiscard]] ReverbNode* Reverb() const { return m_reverb.get(); }

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
        std::shared_ptr<SpatialNode> node;
    };
    /// Commits what changed, simulates each voice's direct path and hands the result to its node, and gives
    /// the worker the listener and the voices for its next round.
    void Simulate(const Listener& listener, std::span<const Voice> voices);
    /// A voice that moved between simulations: its direction and distance now, its occlusion as last found.
    void Place(const Listener& listener, const Voice& voice) const;

    [[nodiscard]] std::size_t TriangleCount() const { return m_triangles; }
    /// Milliseconds of the worker's last round: the reverb and the paths.
    [[nodiscard]] double RoundMilliseconds() const { return m_roundMilliseconds.load(); }

private:
    explicit Scene(const Context& context) : m_context(&context) {}
    [[nodiscard]] DirectParams Placed(const Listener& listener, const Voice& voice) const;
    IPLStaticMesh MakeMesh(IPLScene scene, std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                           const AcousticMaterial& material);
    /// What the game's thread does to the simulator, when the worker is not running it.
    void Apply();
    void ApplyLocked();
    /// The doors' moves; with the simulation locked.
    void ApplyMoves();
    void Work();
    void Bake();

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
    std::size_t m_triangles = 0;
    /// The static geometry's triangles that face up or down, three corners each: where probes may stand.
    std::vector<glm::vec3> m_floors;
    std::unique_ptr<ReverbNode> m_reverb;
    IPLSource m_listenerSource = nullptr; // Reflections traced from the listener: the reverb.

    // The simulator is changed (commits, sources added and removed) only while the worker does not run it.
    std::mutex m_simulation;
    bool m_dirty = true;
    std::vector<IPLSource> m_toAdd;
    std::vector<IPLSource> m_toRemove;
    std::vector<IPLSource> m_added;

    // What the worker simulates next, and the moves of movable meshes not yet in the scene.
    std::mutex m_inputsMutex;
    Listener m_listener;
    std::vector<Voice> m_voices;
    std::vector<std::pair<std::size_t, glm::mat4>> m_moves;

    std::atomic<PathingState> m_pathing{PathingState::None};
    std::atomic<double> m_roundMilliseconds{0.0};
    float m_probeSpacing = 3.0f;
    IPLProbeBatch m_probes = nullptr;

    std::mutex m_wakeMutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::thread m_worker;
};

} // namespace daidalos::audio::steam
