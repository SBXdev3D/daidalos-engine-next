// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "SteamAudio.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>
#include <tuple>

namespace daidalos::audio::steam {

namespace {

constexpr std::string_view kModule = "audio";

IPLVector3 ToIpl(const glm::vec3& v) {
    return {v.x, v.y, v.z};
}

IPLMatrix4x4 ToIpl(const glm::mat4& m) {
    IPLMatrix4x4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.elements[row][column] = m[column][row];
        }
    }
    return result;
}

IPLMaterial ToIpl(const AcousticMaterial& material) {
    IPLMaterial result{};
    for (int band = 0; band < 3; ++band) {
        result.absorption[band] = material.absorption[static_cast<std::size_t>(band)];
        result.transmission[band] = material.transmission[static_cast<std::size_t>(band)];
    }
    result.scattering = material.scattering;
    return result;
}

IPLCoordinateSpace3 ListenerSpace(const Scene::Listener& listener) {
    const glm::vec3 ahead = glm::normalize(listener.forward);
    const glm::vec3 right = glm::normalize(glm::cross(ahead, listener.up));
    const glm::vec3 up = glm::cross(right, ahead);
    return {.right = ToIpl(right), .up = ToIpl(up), .ahead = ToIpl(ahead), .origin = ToIpl(listener.position)};
}

IPLCoordinateSpace3 PointSpace(const glm::vec3& position) {
    return {
        .right = {1.0f, 0.0f, 0.0f}, .up = {0.0f, 1.0f, 0.0f}, .ahead = {0.0f, 0.0f, -1.0f}, .origin = ToIpl(position)};
}

void IPLCALL Log(IPLLogLevel level, const char* message) {
    if (level == IPL_LOGLEVEL_ERROR) {
        DAI_LOG_ERROR(kModule, "Steam Audio: {}", message);
    } else if (level == IPL_LOGLEVEL_WARNING) {
        DAI_LOG_WARNING(kModule, "Steam Audio: {}", message);
    }
}

// Reflections around the listener (ADR-0066): rays, bounces and seconds of tail.
constexpr IPLint32 kReverbRays = 1024;
constexpr IPLint32 kReverbBounces = 8;
// Pathing (ADR-0066): within this range probes see each other, and paths go this far.
constexpr float kVisRange = 30.0f;
constexpr float kPathRange = 60.0f;
// Probes are this far above the floors.
constexpr float kProbeHeight = 1.5f;
// The worker's round, at most this often.
constexpr std::chrono::milliseconds kRound{50};

} // namespace

std::vector<glm::vec3> FloorProbes(std::span<const glm::vec3> floors, float spacing, float height) {
    // Each floor triangle's grid points, a spacing apart in the plane, raised to head height; one probe per
    // cell of the grid and per floor height.
    std::vector<glm::vec3> probes;
    std::set<std::tuple<int, int, int>> taken;
    for (std::size_t index = 0; index + 2 < floors.size(); index += 3) {
        const glm::vec3 a = floors[index];
        const glm::vec3 b = floors[index + 1];
        const glm::vec3 c = floors[index + 2];
        const glm::vec2 low = glm::min(glm::min(glm::vec2(a.x, a.z), glm::vec2(b.x, b.z)), glm::vec2(c.x, c.z));
        const glm::vec2 high = glm::max(glm::max(glm::vec2(a.x, a.z), glm::vec2(b.x, b.z)), glm::vec2(c.x, c.z));
        for (float x = std::ceil(low.x / spacing) * spacing; x <= high.x; x += spacing) {
            for (float z = std::ceil(low.y / spacing) * spacing; z <= high.y; z += spacing) {
                // Barycentric coordinates of (x, z) in the triangle's footprint.
                const glm::vec2 v0(b.x - a.x, b.z - a.z);
                const glm::vec2 v1(c.x - a.x, c.z - a.z);
                const glm::vec2 v2(x - a.x, z - a.z);
                const float d = v0.x * v1.y - v1.x * v0.y;
                if (std::abs(d) < 1e-8f) {
                    continue;
                }
                const float u = (v2.x * v1.y - v1.x * v2.y) / d;
                const float v = (v0.x * v2.y - v2.x * v0.y) / d;
                if (u < 0.0f || v < 0.0f || u + v > 1.0f) {
                    continue;
                }
                const float y = a.y + u * (b.y - a.y) + v * (c.y - a.y) + height;
                const std::tuple<int, int, int> cell{static_cast<int>(std::lround(x / spacing)),
                                                     static_cast<int>(std::lround(z / spacing)),
                                                     static_cast<int>(std::lround(y / height))};
                if (taken.insert(cell).second) {
                    probes.emplace_back(x, y, z);
                }
            }
        }
    }
    return probes;
}

std::unique_ptr<Context> Context::Create(std::uint32_t sampleRate) {
    std::unique_ptr<Context> context(new Context());
    IPLContextSettings settings{};
    settings.version = STEAMAUDIO_VERSION;
    settings.logCallback = Log;
    // AVX-512 slows some CPUs down more than it helps.
    settings.simdLevel = IPL_SIMDLEVEL_AVX2;
    if (iplContextCreate(&settings, &context->m_context) != IPL_STATUS_SUCCESS) {
        DAI_LOG_WARNING(kModule, "Steam Audio did not start: sounds are panned without HRTF or occlusion");
        return nullptr;
    }
    context->m_settings = {.samplingRate = static_cast<IPLint32>(sampleRate), .frameSize = kFrameSize};
    IPLHRTFSettings hrtf{};
    hrtf.type = IPL_HRTFTYPE_DEFAULT;
    hrtf.volume = 1.0f;
    hrtf.normType = IPL_HRTFNORMTYPE_NONE;
    if (iplHRTFCreate(context->m_context, &context->m_settings, &hrtf, &context->m_hrtf) != IPL_STATUS_SUCCESS) {
        DAI_LOG_WARNING(kModule, "Steam Audio has no HRTF: sounds are panned without it");
        return nullptr;
    }
    return context;
}

Context::~Context() {
    iplHRTFRelease(&m_hrtf);
    iplContextRelease(&m_context);
}

// ---- Rings ----------------------------------------------------------------------------------------------

void Ring::Push(const float* frame) {
    if (m_count == kCapacity) {
        return;
    }
    std::memcpy(&m_data[std::size_t{(m_read + m_count) % kCapacity} * m_channels], frame, sizeof(float) * m_channels);
    ++m_count;
}

void Ring::Pop(float* frame) {
    if (m_count == 0) {
        std::memset(frame, 0, sizeof(float) * m_channels);
        return;
    }
    std::memcpy(frame, &m_data[std::size_t{m_read} * m_channels], sizeof(float) * m_channels);
    m_read = (m_read + 1) % kCapacity;
    --m_count;
}

void Ring::PushSilence(std::uint32_t frames) {
    const std::vector<float> zero(m_channels, 0.0f);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        Push(zero.data());
    }
}

// ---- The voices' node ---------------------------------------------------------------------------------

namespace {

ma_node_vtable g_spatialVtable{};
ma_node_vtable g_reverbVtable{};

} // namespace

std::shared_ptr<SpatialNode> SpatialNode::Create(const Context& context, ma_engine* engine, ma_node* target,
                                                 ReverbNode* reverb) {
    std::shared_ptr<SpatialNode> node(new SpatialNode(context));
    IPLAudioSettings settings = context.Settings();
    IPLDirectEffectSettings direct{.numChannels = 1};
    if (iplDirectEffectCreate(context.Get(), &settings, &direct, &node->m_direct) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    IPLBinauralEffectSettings binaural{.hrtf = context.Hrtf()};
    if (iplBinauralEffectCreate(context.Get(), &settings, &binaural, &node->m_binaural) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    IPLPathEffectSettings path{};
    path.maxOrder = 1;
    path.spatialize = IPL_TRUE;
    path.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
    path.hrtf = context.Hrtf();
    if (iplPathEffectCreate(context.Get(), &settings, &path, &node->m_path) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    // The node's first output block is silence: the latency of one block.
    node->m_out.PushSilence(kFrameSize);
    node->m_send.PushSilence(kFrameSize);

    g_spatialVtable.onProcess = &SpatialNode::Process;
    g_spatialVtable.inputBusCount = 1;
    g_spatialVtable.outputBusCount = 2;
    const ma_uint32 channels = ma_engine_get_channels(engine);
    const std::array<ma_uint32, 2> outputs{channels, channels};
    ma_node_config config = ma_node_config_init();
    config.vtable = &g_spatialVtable;
    config.pInputChannels = &channels;
    config.pOutputChannels = outputs.data();
    node->m_node.owner = node.get();
    if (channels != 2 ||
        ma_node_init(ma_engine_get_node_graph(engine), &config, nullptr, &node->m_node.base) != MA_SUCCESS) {
        return nullptr;
    }
    node->m_nodeReady = true;
    ma_node_attach_output_bus(&node->m_node.base, 0, target, 0);
    if (reverb != nullptr) {
        ma_node_attach_output_bus(&node->m_node.base, 1, reverb->Node(), 0);
    }
    return node;
}

SpatialNode::~SpatialNode() {
    if (m_nodeReady) {
        ma_node_uninit(&m_node.base, nullptr);
    }
    iplPathEffectRelease(&m_path);
    iplBinauralEffectRelease(&m_binaural);
    iplDirectEffectRelease(&m_direct);
}

DirectParams SpatialNode::Params() {
    const std::scoped_lock lock(m_paramsMutex);
    return m_params;
}

void SpatialNode::SetParams(const DirectParams& params) {
    const std::scoped_lock lock(m_paramsMutex);
    m_params = params;
}

void SpatialNode::SetPath(const PathParams& path) {
    const std::scoped_lock lock(m_paramsMutex);
    m_pathParams = path;
}

void SpatialNode::Process(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut,
                          ma_uint32* frameCountOut) {
    SpatialNode& self = *reinterpret_cast<NodeBase*>(node)->owner;
    // Mixed to mono, into the ring.
    const ma_uint32 inFrames = frameCountIn != nullptr ? frameCountIn[0] : 0;
    for (ma_uint32 frame = 0; frame < inFrames; ++frame) {
        const float mono = 0.5f * (framesIn[0][frame * 2] + framesIn[0][frame * 2 + 1]);
        self.m_in.Push(&mono);
    }
    while (self.m_in.Count() >= kFrameSize) {
        self.ProcessBlock();
    }
    for (ma_uint32 frame = 0; frame < *frameCountOut; ++frame) {
        self.m_out.Pop(&framesOut[0][frame * 2]);
        float send = 0.0f;
        self.m_send.Pop(&send);
        framesOut[1][frame * 2] = send;
        framesOut[1][frame * 2 + 1] = send;
    }
}

void SpatialNode::ProcessBlock() {
    for (std::uint32_t frame = 0; frame < kFrameSize; ++frame) {
        m_in.Pop(&m_block[frame]);
    }
    // The parameters the other threads last set; if they are setting them now, the last ones.
    if (std::unique_lock lock(m_paramsMutex, std::try_to_lock); lock.owns_lock()) {
        m_audioParams = m_params;
        m_audioPath = m_pathParams;
    }
    const DirectParams& params = m_audioParams;

    float* blockChannels[1] = {m_block.data()};
    float* directedChannels[1] = {m_directed.data()};
    float* stereoChannels[2] = {m_left.data(), m_right.data()};
    float* pathChannels[2] = {m_pathLeft.data(), m_pathRight.data()};
    IPLAudioBuffer in{.numChannels = 1, .numSamples = kFrameSize, .data = blockChannels};
    IPLAudioBuffer directed{.numChannels = 1, .numSamples = kFrameSize, .data = directedChannels};
    IPLAudioBuffer stereo{.numChannels = 2, .numSamples = kFrameSize, .data = stereoChannels};
    IPLAudioBuffer pathed{.numChannels = 2, .numSamples = kFrameSize, .data = pathChannels};

    IPLDirectEffectParams direct{};
    direct.flags = static_cast<IPLDirectEffectFlags>(
        IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION |
        IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION);
    direct.transmissionType = IPL_TRANSMISSIONTYPE_FREQDEPENDENT;
    direct.distanceAttenuation = params.distanceAttenuation;
    std::memcpy(direct.airAbsorption, params.airAbsorption.data(), sizeof(direct.airAbsorption));
    direct.directivity = 1.0f;
    direct.occlusion = params.occlusion;
    std::memcpy(direct.transmission, params.transmission.data(), sizeof(direct.transmission));
    iplDirectEffectApply(m_direct, &direct, &in, &directed);

    IPLBinauralEffectParams binaural{};
    binaural.direction = params.direction;
    binaural.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
    binaural.spatialBlend = 1.0f;
    binaural.hrtf = m_context->Hrtf();
    iplBinauralEffectApply(m_binaural, &binaural, &directed, &stereo);

    // The way around what blocks the direct path, as much as it is blocked (ADR-0066).
    const float around = m_audioPath.valid ? (1.0f - params.occlusion) * params.distanceAttenuation : 0.0f;
    if (around > 0.0f) {
        IPLPathEffectParams path{};
        std::memcpy(path.eqCoeffs, m_audioPath.eq.data(), sizeof(path.eqCoeffs));
        path.shCoeffs = m_audioPath.sh.data();
        path.order = 1;
        path.binaural = IPL_TRUE;
        path.hrtf = m_context->Hrtf();
        path.listener = m_audioPath.listener;
        path.normalizeEQ = IPL_TRUE;
        iplPathEffectApply(m_path, &path, &in, &pathed);
    }
    // To the reverb: the sound itself, quieter with distance, but less than the direct path; and only as much of it
    // as comes through the walls, or a sound in another room rings in this one.
    const float transmitted = (params.transmission[0] + params.transmission[1] + params.transmission[2]) / 3.0f;
    const float through = params.occlusion + (1.0f - params.occlusion) * transmitted;
    const float sendGain = std::sqrt(params.distanceAttenuation) * through;
    for (std::uint32_t frame = 0; frame < kFrameSize; ++frame) {
        float out[2] = {m_left[frame], m_right[frame]};
        if (around > 0.0f) {
            out[0] += m_pathLeft[frame] * around;
            out[1] += m_pathRight[frame] * around;
        }
        m_out.Push(out);
        const float send = m_block[frame] * sendGain;
        m_send.Push(&send);
    }
}

// ---- The reverb ----------------------------------------------------------------------------------------

std::unique_ptr<ReverbNode> ReverbNode::Create(const Context& context, ma_engine* engine, ma_node* target) {
    std::unique_ptr<ReverbNode> node(new ReverbNode(context));
    IPLAudioSettings settings = context.Settings();
    IPLReflectionEffectSettings effect{};
    effect.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
    effect.irSize = static_cast<IPLint32>(kDuration * static_cast<float>(settings.samplingRate));
    effect.numChannels = 1;
    if (iplReflectionEffectCreate(context.Get(), &settings, &effect, &node->m_effect) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    node->m_out.PushSilence(kFrameSize);
    g_reverbVtable.onProcess = &ReverbNode::Process;
    g_reverbVtable.inputBusCount = 1;
    g_reverbVtable.outputBusCount = 1;
    const ma_uint32 channels = ma_engine_get_channels(engine);
    ma_node_config config = ma_node_config_init();
    config.vtable = &g_reverbVtable;
    config.pInputChannels = &channels;
    config.pOutputChannels = &channels;
    node->m_node.owner = node.get();
    if (channels != 2 ||
        ma_node_init(ma_engine_get_node_graph(engine), &config, nullptr, &node->m_node.base) != MA_SUCCESS) {
        return nullptr;
    }
    node->m_nodeReady = true;
    ma_node_attach_output_bus(&node->m_node.base, 0, target, 0);
    return node;
}

ReverbNode::~ReverbNode() {
    if (m_nodeReady) {
        ma_node_uninit(&m_node.base, nullptr);
    }
    iplReflectionEffectRelease(&m_effect);
}

void ReverbNode::SetParams(const ReverbParams& params) {
    const std::scoped_lock lock(m_paramsMutex);
    m_params = params;
}

void ReverbNode::Process(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut,
                         ma_uint32* frameCountOut) {
    ReverbNode& self = *reinterpret_cast<NodeBase*>(node)->owner;
    const ma_uint32 inFrames = frameCountIn != nullptr ? frameCountIn[0] : 0;
    for (ma_uint32 frame = 0; frame < inFrames; ++frame) {
        const float mono = 0.5f * (framesIn[0][frame * 2] + framesIn[0][frame * 2 + 1]);
        self.m_in.Push(&mono);
    }
    while (self.m_in.Count() >= kFrameSize) {
        self.ProcessBlock();
    }
    for (ma_uint32 frame = 0; frame < *frameCountOut; ++frame) {
        self.m_out.Pop(&framesOut[0][frame * 2]);
    }
}

void ReverbNode::ProcessBlock() {
    for (std::uint32_t frame = 0; frame < kFrameSize; ++frame) {
        m_in.Pop(&m_block[frame]);
    }
    if (std::unique_lock lock(m_paramsMutex, std::try_to_lock); lock.owns_lock()) {
        m_audioParams = m_params;
    }
    if (!m_audioParams.valid) {
        const std::array<float, 2> silence{};
        for (std::uint32_t frame = 0; frame < kFrameSize; ++frame) {
            m_out.Push(silence.data());
        }
        return;
    }
    float* blockChannels[1] = {m_block.data()};
    float* wetChannels[1] = {m_wet.data()};
    IPLAudioBuffer in{.numChannels = 1, .numSamples = kFrameSize, .data = blockChannels};
    IPLAudioBuffer wet{.numChannels = 1, .numSamples = kFrameSize, .data = wetChannels};
    IPLReflectionEffectParams params{};
    params.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
    std::memcpy(params.reverbTimes, m_audioParams.reverbTimes.data(), sizeof(params.reverbTimes));
    params.numChannels = 1;
    params.irSize = static_cast<IPLint32>(kDuration * static_cast<float>(m_context->Settings().samplingRate));
    iplReflectionEffectApply(m_effect, &params, &in, &wet, nullptr);
    for (std::uint32_t frame = 0; frame < kFrameSize; ++frame) {
        const std::array<float, 2> out{m_wet[frame] * kWet, m_wet[frame] * kWet};
        m_out.Push(out.data());
    }
}

// ---- The world's geometry and simulation --------------------------------------------------------------

std::unique_ptr<Scene> Scene::Create(const Context& context, ma_engine* engine, ma_node* target) {
    std::unique_ptr<Scene> scene(new Scene(context));
    IPLSceneSettings settings{};
    settings.type = IPL_SCENETYPE_DEFAULT;
    if (iplSceneCreate(context.Get(), &settings, &scene->m_scene) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    IPLSimulationSettings simulation{};
    simulation.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS |
                                                       IPL_SIMULATIONFLAGS_PATHING);
    simulation.sceneType = IPL_SCENETYPE_DEFAULT;
    simulation.reflectionType = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
    simulation.maxNumOcclusionSamples = 32;
    simulation.maxNumRays = kReverbRays;
    simulation.numDiffuseSamples = 32;
    simulation.maxDuration = ReverbNode::kDuration;
    simulation.maxOrder = 1;
    simulation.maxNumSources = 256;
    simulation.numThreads = 2;
    simulation.numVisSamples = 4;
    simulation.samplingRate = context.Settings().samplingRate;
    simulation.frameSize = kFrameSize;
    if (iplSimulatorCreate(context.Get(), &simulation, &scene->m_simulator) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    iplSimulatorSetScene(scene->m_simulator, scene->m_scene);
    scene->m_reverb = ReverbNode::Create(context, engine, target);
    IPLSourceSettings listenerSource{.flags = IPL_SIMULATIONFLAGS_REFLECTIONS};
    if (iplSourceCreate(scene->m_simulator, &listenerSource, &scene->m_listenerSource) == IPL_STATUS_SUCCESS) {
        iplSourceAdd(scene->m_listenerSource, scene->m_simulator);
    }
    scene->m_worker = std::thread([raw = scene.get()] { raw->Work(); });
    return scene;
}

Scene::~Scene() {
    {
        const std::scoped_lock lock(m_wakeMutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_voices.clear();
    for (IPLSource& source : m_toAdd) {
        iplSourceRelease(&source);
    }
    // A source whose removal waited for the simulation is still among the added ones: released once, here.
    for (IPLSource source : m_toRemove) {
        iplSourceRemove(source, m_simulator);
        std::erase(m_added, source);
        iplSourceRelease(&source);
    }
    for (IPLSource& source : m_added) {
        iplSourceRemove(source, m_simulator);
        iplSourceRelease(&source);
    }
    if (m_listenerSource != nullptr) {
        iplSourceRemove(m_listenerSource, m_simulator);
        iplSourceRelease(&m_listenerSource);
    }
    if (m_probes != nullptr) {
        iplSimulatorRemoveProbeBatch(m_simulator, m_probes);
        iplProbeBatchRelease(&m_probes);
    }
    for (Movable& movable : m_movable) {
        iplInstancedMeshRemove(movable.instance, m_scene);
        iplInstancedMeshRelease(&movable.instance);
        iplStaticMeshRelease(&movable.mesh);
        iplSceneRelease(&movable.subScene);
    }
    for (IPLStaticMesh& mesh : m_static) {
        iplStaticMeshRemove(mesh, m_scene);
        iplStaticMeshRelease(&mesh);
    }
    iplSimulatorRelease(&m_simulator);
    iplSceneRelease(&m_scene);
    m_reverb.reset();
}

IPLStaticMesh Scene::MakeMesh(IPLScene scene, std::span<const glm::vec3> positions,
                              std::span<const std::uint32_t> indices, const AcousticMaterial& material) {
    std::vector<IPLVector3> vertices;
    vertices.reserve(positions.size());
    for (const glm::vec3& position : positions) {
        vertices.push_back(ToIpl(position));
    }
    std::vector<IPLTriangle> triangles;
    triangles.reserve(indices.size() / 3);
    for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
        triangles.push_back({{static_cast<IPLint32>(indices[index]), static_cast<IPLint32>(indices[index + 1]),
                              static_cast<IPLint32>(indices[index + 2])}});
    }
    std::vector<IPLint32> materialIndices(triangles.size(), 0);
    IPLMaterial ipl = ToIpl(material);
    IPLStaticMeshSettings settings{};
    settings.numVertices = static_cast<IPLint32>(vertices.size());
    settings.numTriangles = static_cast<IPLint32>(triangles.size());
    settings.numMaterials = 1;
    settings.vertices = vertices.data();
    settings.triangles = triangles.data();
    settings.materialIndices = materialIndices.data();
    settings.materials = &ipl;
    IPLStaticMesh mesh = nullptr;
    if (triangles.empty() || iplStaticMeshCreate(scene, &settings, &mesh) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    m_triangles += triangles.size();
    return mesh;
}

void Scene::AddStatic(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                      const AcousticMaterial& material) {
    // The scene changes only while the worker does not trace it.
    const std::scoped_lock lock(m_simulation);
    if (IPLStaticMesh mesh = MakeMesh(m_scene, positions, indices, material)) {
        iplStaticMeshAdd(mesh, m_scene);
        m_static.push_back(mesh);
        // Triangles that face up are floors, where probes for pathing go.
        for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
            const glm::vec3& a = positions[indices[index]];
            const glm::vec3& b = positions[indices[index + 1]];
            const glm::vec3& c = positions[indices[index + 2]];
            const glm::vec3 normal = glm::cross(b - a, c - a);
            if (glm::length(normal) > 1e-6f && std::abs(glm::normalize(normal).y) > 0.8f) {
                m_floors.insert(m_floors.end(), {a, b, c});
            }
        }
        m_dirty = true;
    }
}

std::size_t Scene::AddMovable(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                              const AcousticMaterial& material, const glm::mat4& transform) {
    const std::scoped_lock lock(m_simulation);
    Movable movable;
    IPLSceneSettings settings{};
    settings.type = IPL_SCENETYPE_DEFAULT;
    if (iplSceneCreate(m_context->Get(), &settings, &movable.subScene) != IPL_STATUS_SUCCESS) {
        return m_movable.size();
    }
    movable.mesh = MakeMesh(movable.subScene, positions, indices, material);
    if (movable.mesh == nullptr) {
        iplSceneRelease(&movable.subScene);
        return m_movable.size();
    }
    iplStaticMeshAdd(movable.mesh, movable.subScene);
    iplSceneCommit(movable.subScene);
    IPLInstancedMeshSettings instance{.subScene = movable.subScene, .transform = ToIpl(transform)};
    if (iplInstancedMeshCreate(m_scene, &instance, &movable.instance) != IPL_STATUS_SUCCESS) {
        iplStaticMeshRelease(&movable.mesh);
        iplSceneRelease(&movable.subScene);
        return m_movable.size();
    }
    iplInstancedMeshAdd(movable.instance, m_scene);
    m_movable.push_back(movable);
    m_dirty = true;
    return m_movable.size() - 1;
}

void Scene::Move(std::size_t movable, const glm::mat4& transform) {
    if (movable >= m_movable.size()) {
        return;
    }
    // The transform waits for the next commit: a door moving while the worker traces is not worth a stall.
    {
        const std::scoped_lock lock(m_inputsMutex);
        m_moves.emplace_back(movable, transform);
    }
    Apply();
}

void Scene::ApplyMoves() {
    std::vector<std::pair<std::size_t, glm::mat4>> moves;
    {
        const std::scoped_lock lock(m_inputsMutex);
        moves.swap(m_moves);
    }
    for (const auto& [movable, transform] : moves) {
        iplInstancedMeshUpdateTransform(m_movable[movable].instance, m_scene, ToIpl(transform));
        m_dirty = true;
    }
}

void Scene::StartPathing(float spacing) {
    PathingState expected = PathingState::None;
    if (m_floors.empty() || !m_pathing.compare_exchange_strong(expected, PathingState::Baking)) {
        return;
    }
    m_probeSpacing = std::max(spacing, 0.5f);
    m_wake.notify_all();
}

PathingState Scene::Pathing() const {
    return m_pathing.load();
}

IPLSource Scene::AddSource() {
    IPLSourceSettings settings{
        .flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_PATHING)};
    IPLSource source = nullptr;
    if (iplSourceCreate(m_simulator, &settings, &source) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    m_toAdd.push_back(source);
    Apply();
    return source;
}

void Scene::RemoveSource(IPLSource source) {
    if (source == nullptr) {
        return;
    }
    if (const auto pending = std::ranges::find(m_toAdd, source); pending != m_toAdd.end()) {
        m_toAdd.erase(pending);
        iplSourceRelease(&source);
        return;
    }
    m_toRemove.push_back(source);
    Apply();
}

void Scene::Apply() {
    const std::unique_lock lock(m_simulation, std::try_to_lock);
    if (lock.owns_lock()) {
        ApplyLocked();
    }
}

void Scene::ApplyLocked() {
    for (IPLSource source : m_toRemove) {
        iplSourceRemove(source, m_simulator);
        std::erase(m_added, source);
        iplSourceRelease(&source);
    }
    m_toRemove.clear();
    for (IPLSource source : m_toAdd) {
        iplSourceAdd(source, m_simulator);
        m_added.push_back(source);
    }
    if (!m_toAdd.empty()) {
        m_dirty = true;
    }
    m_toAdd.clear();
    ApplyMoves();
    if (m_dirty) {
        iplSceneCommit(m_scene);
        iplSimulatorCommit(m_simulator);
        m_dirty = false;
    }
}

DirectParams Scene::Placed(const Listener& listener, const Voice& voice) const {
    DirectParams params = voice.node->Params();
    const glm::vec3 offset = voice.position - listener.position;
    if (glm::dot(offset, offset) > 1e-8f) {
        const glm::vec3 ahead = glm::normalize(listener.forward);
        params.direction = iplCalculateRelativeDirection(m_context->Get(), ToIpl(voice.position),
                                                         ToIpl(listener.position), ToIpl(ahead), ToIpl(listener.up));
    } else {
        params.direction = {0.0f, 0.0f, -1.0f};
    }
    // As miniaudio's inverse model did, full volume within the minimum; past the maximum, it fades out, gone at
    // twice the maximum, so that a level's far rooms are not all heard at once (ADR-0123's follow-up).
    const float distance = glm::length(offset);
    const float maximum = std::max(voice.maxDistance, voice.minDistance);
    const float beyond = maximum > 0.0f ? std::clamp((distance - maximum) / maximum, 0.0f, 1.0f) : 0.0f;
    params.distanceAttenuation = voice.minDistance / std::clamp(distance, voice.minDistance, maximum) * (1.0f - beyond);
    return params;
}

void Scene::Place(const Listener& listener, const Voice& voice) const {
    voice.node->SetParams(Placed(listener, voice));
}

void Scene::Simulate(const Listener& listener, std::span<const Voice> voices) {
    DAI_PROFILE_SCOPE("audio::Simulate");
    {
        // The worker gets the listener and the voices for its next round.
        const std::scoped_lock inputs(m_inputsMutex);
        m_listener = listener;
        m_voices.assign(voices.begin(), voices.end());
    }
    // While the worker runs the simulator (a round, or baking paths), voices only take their direction and
    // distance: the simulator must not run the direct path while it commits.
    const std::unique_lock lock(m_simulation, std::try_to_lock);
    if (!lock.owns_lock()) {
        for (const Voice& voice : voices) {
            Place(listener, voice);
        }
        return;
    }
    ApplyLocked();
    IPLSimulationSharedInputs shared{};
    shared.listener = ListenerSpace(listener);
    iplSimulatorSetSharedInputs(m_simulator, IPL_SIMULATIONFLAGS_DIRECT, &shared);
    std::vector<const Voice*> simulated;
    for (const Voice& voice : voices) {
        // A source not added yet takes its direction and distance only.
        if (std::ranges::find(m_added, voice.source) == m_added.end()) {
            Place(listener, voice);
            continue;
        }
        simulated.push_back(&voice);
        IPLSimulationInputs inputs{};
        inputs.flags = IPL_SIMULATIONFLAGS_DIRECT;
        inputs.directFlags = static_cast<IPLDirectSimulationFlags>(IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION |
                                                                   IPL_DIRECTSIMULATIONFLAGS_OCCLUSION |
                                                                   IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);
        inputs.source = PointSpace(voice.position);
        inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
        // A source is a ball, not a point: occlusion fades in as a wall covers it.
        inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
        inputs.occlusionRadius = 0.3f;
        inputs.numOcclusionSamples = 16;
        // Transmission through up to two walls, the nearest first.
        inputs.numTransmissionRays = 2;
        iplSourceSetInputs(voice.source, IPL_SIMULATIONFLAGS_DIRECT, &inputs);
    }
    if (!simulated.empty()) {
        iplSimulatorRunDirect(m_simulator);
    }
    for (const Voice* voice : simulated) {
        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(voice->source, IPL_SIMULATIONFLAGS_DIRECT, &outputs);
        DirectParams params = Placed(listener, *voice);
        for (std::size_t band = 0; band < 3; ++band) {
            params.airAbsorption[band] = outputs.direct.airAbsorption[band];
            params.transmission[band] = outputs.direct.transmission[band];
        }
        params.occlusion = outputs.direct.occlusion;
        voice->node->SetParams(params);
    }
}

void Scene::Bake() {
    const auto start = std::chrono::steady_clock::now();
    const std::scoped_lock lock(m_simulation);
    iplSceneCommit(m_scene);
    // Probes over every floor, at the height of a head: Steam Audio's own placement looks down from above the
    // whole level, and finds only its roofs.
    const std::vector<glm::vec3> centers = FloorProbes(m_floors, m_probeSpacing, kProbeHeight);
    IPLProbeBatch batch = nullptr;
    if (centers.empty() || iplProbeBatchCreate(m_context->Get(), &batch) != IPL_STATUS_SUCCESS) {
        DAI_LOG_WARNING(kModule, "No pathing: no floor for probes");
        m_pathing = PathingState::Failed;
        return;
    }
    for (const glm::vec3& center : centers) {
        iplProbeBatchAddProbe(batch, IPLSphere{ToIpl(center), m_probeSpacing});
    }
    const auto probes = centers.size();
    iplProbeBatchCommit(batch);

    IPLPathBakeParams bake{};
    bake.scene = m_scene;
    bake.probeBatch = batch;
    bake.identifier.type = IPL_BAKEDDATATYPE_PATHING;
    bake.identifier.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
    bake.numSamples = 4;
    bake.radius = 0.5f;
    bake.threshold = 0.1f;
    bake.visRange = kVisRange;
    bake.pathRange = kPathRange;
    bake.numThreads = static_cast<IPLint32>(std::max(1U, std::thread::hardware_concurrency() / 2));
    // Steam Audio 4.8.1 crashes without a progress callback.
    iplPathBakerBake(m_context->Get(), &bake, [](IPLfloat32, void*) {}, nullptr);
    iplSimulatorAddProbeBatch(m_simulator, batch);
    iplSimulatorCommit(m_simulator);
    m_probes = batch;
    m_pathing = PathingState::Ready;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    DAI_LOG_INFO(kModule, "Pathing baked: {} probes {} m apart, in {:.1f} s", probes, m_probeSpacing, seconds);
}

void Scene::Work() {
    while (true) {
        {
            std::unique_lock wake(m_wakeMutex);
            m_wake.wait_for(wake, kRound, [this] { return m_stop; });
            if (m_stop) {
                return;
            }
        }
        if (m_pathing.load() == PathingState::Baking) {
            Bake();
        }
        const std::scoped_lock lock(m_simulation);
        const auto roundStart = std::chrono::steady_clock::now();
        Listener listener;
        std::vector<Voice> voices;
        {
            const std::scoped_lock inputs(m_inputsMutex);
            listener = m_listener;
            voices = m_voices;
        }
        // Commits wait for the worker: what the game thread could not commit, the worker does.
        ApplyMoves();
        if (m_dirty) {
            iplSceneCommit(m_scene);
            iplSimulatorCommit(m_simulator);
            m_dirty = false;
        }
        IPLSimulationSharedInputs shared{};
        shared.listener = ListenerSpace(listener);
        shared.numRays = kReverbRays;
        shared.numBounces = kReverbBounces;
        shared.duration = ReverbNode::kDuration;
        shared.order = 1;
        shared.irradianceMinDistance = 1.0f;
        iplSimulatorSetSharedInputs(
            m_simulator, static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING),
            &shared);
        // The reverb: reflections of a source where the listener is.
        IPLSimulationInputs reverb{};
        reverb.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
        reverb.source = PointSpace(listener.position);
        reverb.reverbScale[0] = reverb.reverbScale[1] = reverb.reverbScale[2] = 1.0f;
        iplSourceSetInputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &reverb);
        const bool pathing = m_pathing.load() == PathingState::Ready;
        std::vector<const Voice*> pathed;
        if (pathing) {
            for (const Voice& voice : voices) {
                if (std::ranges::find(m_added, voice.source) == m_added.end()) {
                    continue;
                }
                pathed.push_back(&voice);
                IPLSimulationInputs inputs{};
                inputs.flags = IPL_SIMULATIONFLAGS_PATHING;
                inputs.source = PointSpace(voice.position);
                inputs.pathingProbes = m_probes;
                inputs.visRadius = 0.5f;
                inputs.visThreshold = 0.1f;
                inputs.visRange = kVisRange;
                inputs.pathingOrder = 1;
                inputs.enableValidation = IPL_TRUE;
                inputs.findAlternatePaths = IPL_TRUE;
                iplSourceSetInputs(voice.source, IPL_SIMULATIONFLAGS_PATHING, &inputs);
            }
        }
        iplSimulatorRunReflections(m_simulator);
        if (pathing && !pathed.empty()) {
            iplSimulatorRunPathing(m_simulator);
        }
        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);
        ReverbParams params;
        params.valid = true;
        for (std::size_t band = 0; band < 3; ++band) {
            params.reverbTimes[band] = outputs.reflections.reverbTimes[band];
        }
        if (m_reverb != nullptr) {
            m_reverb->SetParams(params);
        }
        for (const Voice* voice : pathed) {
            IPLSimulationOutputs voiceOutputs{};
            iplSourceGetOutputs(voice->source, IPL_SIMULATIONFLAGS_PATHING, &voiceOutputs);
            PathParams path;
            path.valid = voiceOutputs.pathing.shCoeffs != nullptr;
            for (std::size_t band = 0; band < 3; ++band) {
                path.eq[band] = voiceOutputs.pathing.eqCoeffs[band];
            }
            if (path.valid) {
                std::memcpy(path.sh.data(), voiceOutputs.pathing.shCoeffs, sizeof(float) * path.sh.size());
            }
            path.listener = shared.listener;
            voice->node->SetPath(path);
        }
        m_roundMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - roundStart).count();
    }
}

} // namespace daidalos::audio::steam
