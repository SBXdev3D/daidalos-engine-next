// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "SteamAudio.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstring>

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

void IPLCALL Log(IPLLogLevel level, const char* message) {
    if (level == IPL_LOGLEVEL_ERROR) {
        DAI_LOG_ERROR(kModule, "Steam Audio: {}", message);
    } else if (level == IPL_LOGLEVEL_WARNING) {
        DAI_LOG_WARNING(kModule, "Steam Audio: {}", message);
    }
}

} // namespace

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

// ---- The voices' node ---------------------------------------------------------------------------------

namespace {

ma_node_vtable g_spatialVtable{};

} // namespace

std::unique_ptr<SpatialNode> SpatialNode::Create(const Context& context, ma_engine* engine, ma_node* target) {
    std::unique_ptr<SpatialNode> node(new SpatialNode(context));
    IPLAudioSettings settings = context.Settings();
    IPLDirectEffectSettings direct{.numChannels = 1};
    if (iplDirectEffectCreate(context.Get(), &settings, &direct, &node->m_direct) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    IPLBinauralEffectSettings binaural{.hrtf = context.Hrtf()};
    if (iplBinauralEffectCreate(context.Get(), &settings, &binaural, &node->m_binaural) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    // The node's first output block is silence: the latency of one block.
    node->m_outCount = kFrameSize;

    g_spatialVtable.onProcess = &SpatialNode::Process;
    g_spatialVtable.inputBusCount = 1;
    g_spatialVtable.outputBusCount = 1;
    const ma_uint32 channels = ma_engine_get_channels(engine);
    ma_node_config config = ma_node_config_init();
    config.vtable = &g_spatialVtable;
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

SpatialNode::~SpatialNode() {
    if (m_nodeReady) {
        ma_node_uninit(&m_node.base, nullptr);
    }
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

void SpatialNode::Process(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut,
                          ma_uint32* frameCountOut) {
    SpatialNode& self = *reinterpret_cast<NodeBase*>(node)->owner;
    // Mixed to mono, into the ring.
    const ma_uint32 inFrames = frameCountIn != nullptr ? frameCountIn[0] : 0;
    for (ma_uint32 frame = 0; frame < inFrames && self.m_inCount < kRing; ++frame) {
        const float mono = 0.5f * (framesIn[0][frame * 2] + framesIn[0][frame * 2 + 1]);
        self.m_in[(self.m_inRead + self.m_inCount) % kRing] = mono;
        ++self.m_inCount;
    }
    while (self.m_inCount >= kFrameSize) {
        self.ProcessBlock();
    }
    const ma_uint32 outFrames = *frameCountOut;
    for (ma_uint32 frame = 0; frame < outFrames; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        if (self.m_outCount > 0) {
            left = self.m_out[std::size_t{self.m_outRead} * 2];
            right = self.m_out[std::size_t{self.m_outRead} * 2 + 1];
            self.m_outRead = (self.m_outRead + 1) % kRing;
            --self.m_outCount;
        }
        framesOut[0][frame * 2] = left;
        framesOut[0][frame * 2 + 1] = right;
    }
}

void SpatialNode::ProcessBlock() {
    for (std::uint32_t frame = 0; frame < kFrameSize; ++frame) {
        m_block[frame] = m_in[(m_inRead + frame) % kRing];
    }
    m_inRead = (m_inRead + kFrameSize) % kRing;
    m_inCount -= kFrameSize;
    // The parameters the game's thread last set; if it is setting them now, the last ones.
    if (std::unique_lock lock(m_paramsMutex, std::try_to_lock); lock.owns_lock()) {
        m_audioParams = m_params;
    }
    const DirectParams& params = m_audioParams;

    float* blockChannels[1] = {m_block.data()};
    float* directedChannels[1] = {m_directed.data()};
    float* stereoChannels[2] = {m_left.data(), m_right.data()};
    IPLAudioBuffer in{.numChannels = 1, .numSamples = kFrameSize, .data = blockChannels};
    IPLAudioBuffer directed{.numChannels = 1, .numSamples = kFrameSize, .data = directedChannels};
    IPLAudioBuffer stereo{.numChannels = 2, .numSamples = kFrameSize, .data = stereoChannels};

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

    for (std::uint32_t frame = 0; frame < kFrameSize && m_outCount < kRing; ++frame) {
        const std::uint32_t at = (m_outRead + m_outCount) % kRing;
        m_out[std::size_t{at} * 2] = m_left[frame];
        m_out[std::size_t{at} * 2 + 1] = m_right[frame];
        ++m_outCount;
    }
}

// ---- The world's geometry and simulation --------------------------------------------------------------

std::unique_ptr<Scene> Scene::Create(const Context& context) {
    std::unique_ptr<Scene> scene(new Scene(context));
    IPLSceneSettings settings{};
    settings.type = IPL_SCENETYPE_DEFAULT;
    if (iplSceneCreate(context.Get(), &settings, &scene->m_scene) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    IPLSimulationSettings simulation{};
    simulation.flags = IPL_SIMULATIONFLAGS_DIRECT;
    simulation.sceneType = IPL_SCENETYPE_DEFAULT;
    simulation.reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
    simulation.maxNumOcclusionSamples = 32;
    simulation.maxNumSources = 256;
    simulation.numThreads = 1;
    simulation.samplingRate = context.Settings().samplingRate;
    simulation.frameSize = kFrameSize;
    if (iplSimulatorCreate(context.Get(), &simulation, &scene->m_simulator) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    iplSimulatorSetScene(scene->m_simulator, scene->m_scene);
    return scene;
}

Scene::~Scene() {
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
    if (IPLStaticMesh mesh = MakeMesh(m_scene, positions, indices, material)) {
        iplStaticMeshAdd(mesh, m_scene);
        m_static.push_back(mesh);
        m_dirty = true;
    }
}

std::size_t Scene::AddMovable(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                              const AcousticMaterial& material, const glm::mat4& transform) {
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
    if (movable < m_movable.size()) {
        iplInstancedMeshUpdateTransform(m_movable[movable].instance, m_scene, ToIpl(transform));
        m_dirty = true;
    }
}

IPLSource Scene::AddSource() {
    IPLSourceSettings settings{.flags = IPL_SIMULATIONFLAGS_DIRECT};
    IPLSource source = nullptr;
    if (iplSourceCreate(m_simulator, &settings, &source) != IPL_STATUS_SUCCESS) {
        return nullptr;
    }
    iplSourceAdd(source, m_simulator);
    m_dirty = true;
    return source;
}

void Scene::RemoveSource(IPLSource source) {
    if (source != nullptr) {
        iplSourceRemove(source, m_simulator);
        iplSourceRelease(&source);
        m_dirty = true;
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
    // As miniaudio's inverse model did: full volume within the minimum, no quieter past the maximum.
    const float distance = glm::length(offset);
    params.distanceAttenuation =
        voice.minDistance / std::clamp(distance, voice.minDistance, std::max(voice.maxDistance, voice.minDistance));
    return params;
}

void Scene::Place(const Listener& listener, const Voice& voice) const {
    voice.node->SetParams(Placed(listener, voice));
}

void Scene::Simulate(const Listener& listener, std::span<const Voice> voices) {
    DAI_PROFILE_SCOPE("audio::Simulate");
    if (m_dirty) {
        iplSceneCommit(m_scene);
        iplSimulatorCommit(m_simulator);
        m_dirty = false;
    }
    const glm::vec3 ahead = glm::normalize(listener.forward);
    const glm::vec3 right = glm::normalize(glm::cross(ahead, listener.up));
    const glm::vec3 up = glm::cross(right, ahead);
    IPLCoordinateSpace3 listenerSpace{
        .right = ToIpl(right), .up = ToIpl(up), .ahead = ToIpl(ahead), .origin = ToIpl(listener.position)};
    IPLSimulationSharedInputs shared{};
    shared.listener = listenerSpace;
    iplSimulatorSetSharedInputs(m_simulator, IPL_SIMULATIONFLAGS_DIRECT, &shared);
    for (const Voice& voice : voices) {
        IPLSimulationInputs inputs{};
        inputs.flags = IPL_SIMULATIONFLAGS_DIRECT;
        inputs.directFlags = static_cast<IPLDirectSimulationFlags>(IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION |
                                                                   IPL_DIRECTSIMULATIONFLAGS_OCCLUSION |
                                                                   IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);
        inputs.source = {.right = {1.0f, 0.0f, 0.0f},
                         .up = {0.0f, 1.0f, 0.0f},
                         .ahead = {0.0f, 0.0f, -1.0f},
                         .origin = ToIpl(voice.position)};
        inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
        // A source is a ball, not a point: occlusion fades in as a wall covers it.
        inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
        inputs.occlusionRadius = 0.3f;
        inputs.numOcclusionSamples = 16;
        // Transmission through up to two walls, the nearest first.
        inputs.numTransmissionRays = 2;
        iplSourceSetInputs(voice.source, IPL_SIMULATIONFLAGS_DIRECT, &inputs);
    }
    iplSimulatorRunDirect(m_simulator);
    for (const Voice& voice : voices) {
        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(voice.source, IPL_SIMULATIONFLAGS_DIRECT, &outputs);
        DirectParams params = Placed(listener, voice);
        for (std::size_t band = 0; band < 3; ++band) {
            params.airAbsorption[band] = outputs.direct.airAbsorption[band];
            params.transmission[band] = outputs.direct.transmission[band];
        }
        params.occlusion = outputs.direct.occlusion;
        voice.node->SetParams(params);
    }
}

} // namespace daidalos::audio::steam
