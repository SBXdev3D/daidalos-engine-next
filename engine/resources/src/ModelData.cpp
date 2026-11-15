// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/resources/AssetPath.hpp>
#include <daidalos/resources/ModelData.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec4.hpp>

#include <simdjson.h>

#include <algorithm>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

namespace daidalos::resources {

namespace {

constexpr std::string_view kModule = "resources";

// What a glTF file's extras say, which fastgltf leaves to the application.
struct Extras {
    // Materials whose extras hold {"light": false}: their emission lights nothing (ADR-0044).
    std::set<std::size_t> unlitMaterials;
    // Materials whose extras name what they are made of: {"surface": "metal"} (ADR-0053).
    std::map<std::size_t, std::string> surfaces;
};

void ReadExtras(simdjson::dom::object* extras, std::size_t objectIndex, fastgltf::Category objectType,
                void* userPointer) {
    if (objectType != fastgltf::Category::Materials || extras == nullptr) {
        return;
    }
    bool light = true;
    if ((*extras)["light"].get_bool().get(light) == simdjson::SUCCESS && !light) {
        static_cast<Extras*>(userPointer)->unlitMaterials.insert(objectIndex);
    }
    std::string_view surface;
    if ((*extras)["surface"].get_string().get(surface) == simdjson::SUCCESS) {
        static_cast<Extras*>(userPointer)->surfaces[objectIndex] = std::string(surface);
    }
}

// One image can be used in several ways, and each one is a separate decoded texture.
using ImageKey = std::pair<std::size_t, TextureUse>;

class ModelBuilder {
public:
    ModelBuilder(const fastgltf::Asset& asset, std::filesystem::path directory, std::string fileName, Extras extras)
        : m_asset(asset), m_directory(std::move(directory)), m_fileName(std::move(fileName)),
          m_extras(std::move(extras)) {}

    core::Result<ModelData> Build();

private:
    core::Result<void> AddMaterials();
    core::Result<void> AddMeshes();
    core::Result<void> AddNode(std::size_t index, std::optional<std::uint32_t> parent, std::size_t depth);
    core::Result<void> AddSkins();
    core::Result<void> AddAnimations();
    void AddLights();
    core::Result<std::optional<std::uint32_t>> AddTexture(std::size_t textureIndex, TextureUse use);
    core::Result<ImageSource> MakeImageSource(std::size_t imageIndex, TextureUse use) const;
    core::Result<MeshData> ReadPrimitive(const fastgltf::Primitive& primitive, const std::string& name) const;

    [[nodiscard]] tl::unexpected<core::Error> Invalid(const std::string& message) const {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("Model '{}': {}", m_fileName, message));
    }

    const fastgltf::Asset& m_asset;
    std::filesystem::path m_directory;
    std::string m_fileName;
    Extras m_extras;
    ModelData m_model;
    std::map<ImageKey, std::uint32_t> m_images;
    // For each glTF mesh, its primitives in the model.
    std::vector<std::vector<std::uint32_t>> m_meshPrimitives;
    // For each glTF node, its index in the model, if the scene holds it.
    std::vector<std::optional<std::uint32_t>> m_nodeIndices;
};

core::Result<ImageSource> ModelBuilder::MakeImageSource(std::size_t imageIndex, TextureUse use) const {
    const fastgltf::Image& image = m_asset.images[imageIndex];
    ImageSource source;
    source.use = use;
    source.name = image.name.empty() ? std::format("image {}", imageIndex) : std::string(image.name);

    const auto embed = [&source](std::span<const std::byte> bytes) {
        source.embedded.assign(bytes.begin(), bytes.end());
    };
    bool valid = true;
    std::visit(
        fastgltf::visitor{
            [&](const fastgltf::sources::URI& uri) {
                if (uri.uri.isLocalPath()) {
                    source.file = (m_directory / uri.uri.fspath()).lexically_normal();
                } else {
                    valid = false;
                }
            },
            [&](const fastgltf::sources::Array& array) { embed({array.bytes.data(), array.bytes.size_bytes()}); },
            [&](const fastgltf::sources::Vector& vector) { embed({vector.bytes.data(), vector.bytes.size()}); },
            [&](const fastgltf::sources::BufferView& view) {
                const auto bytes = fastgltf::DefaultBufferDataAdapter{}(m_asset, view.bufferViewIndex);
                embed(std::span(bytes.data(), bytes.size()));
            },
            [&](const auto& /*other*/) { valid = false; },
        },
        image.data);
    if (!valid) {
        return Invalid(std::format("image {} has an unsupported source", imageIndex));
    }
    return source;
}

core::Result<std::optional<std::uint32_t>> ModelBuilder::AddTexture(std::size_t textureIndex, TextureUse use) {
    const fastgltf::Texture& texture = m_asset.textures[textureIndex];
    const fastgltf::Optional<std::size_t>& image =
        texture.basisuImageIndex.has_value() ? texture.basisuImageIndex : texture.imageIndex;
    if (!image.has_value()) {
        return Invalid(std::format("texture {} has no image", textureIndex));
    }

    const ImageKey key{*image, use};
    if (const auto found = m_images.find(key); found != m_images.end()) {
        return found->second;
    }
    DAI_TRY_ASSIGN(ImageSource source, MakeImageSource(*image, use));
    const auto index = static_cast<std::uint32_t>(m_model.images.size());
    m_model.images.push_back(std::move(source));
    m_images.emplace(key, index);
    return index;
}

core::Result<void> ModelBuilder::AddMaterials() {
    m_model.materials.reserve(m_asset.materials.size());
    for (std::size_t index = 0; index < m_asset.materials.size(); ++index) {
        const fastgltf::Material& material = m_asset.materials[index];
        MaterialData data;
        data.name = material.name;
        data.emissiveLight = !m_extras.unlitMaterials.contains(index);
        if (const auto surface = m_extras.surfaces.find(index); surface != m_extras.surfaces.end()) {
            data.surface = surface->second;
        }
        const auto& pbr = material.pbrData;
        data.baseColorFactor = {pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2],
                                pbr.baseColorFactor[3]};
        // In nits (ADR-0043), with KHR_materials_emissive_strength's factor, 1 without it.
        data.emissiveFactor =
            glm::vec3(material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2]) *
            static_cast<float>(material.emissiveStrength);
        data.metallicFactor = pbr.metallicFactor;
        data.roughnessFactor = pbr.roughnessFactor;
        data.doubleSided = material.doubleSided;
        data.ior = material.ior;
        if (material.transmission) {
            data.transmission = material.transmission->transmissionFactor;
        }
        if (material.volume) {
            data.thickness = material.volume->thicknessFactor;
            data.attenuationColor = {material.volume->attenuationColor[0], material.volume->attenuationColor[1],
                                     material.volume->attenuationColor[2]};
            data.attenuationDistance = material.volume->attenuationDistance;
        }
        if (material.alphaMode == fastgltf::AlphaMode::Mask) {
            data.alphaCutoff = material.alphaCutoff;
        } else if (material.alphaMode == fastgltf::AlphaMode::Blend) {
            DAI_LOG_WARNING(kModule, "Model '{}': material '{}' is drawn opaque, transparency comes later", m_fileName,
                            data.name);
        }

        const auto addTexture = [this](std::size_t texture, TextureUse use, std::optional<std::uint32_t>& out) {
            return AddTexture(texture, use).map([&out](std::optional<std::uint32_t> image) { out = image; });
        };
        core::Result<void> added;
        if (pbr.baseColorTexture.has_value()) {
            added = addTexture(pbr.baseColorTexture->textureIndex, TextureUse::Color, data.baseColorImage);
        }
        if (added && pbr.metallicRoughnessTexture.has_value()) {
            added =
                addTexture(pbr.metallicRoughnessTexture->textureIndex, TextureUse::Data, data.metallicRoughnessImage);
        }
        if (added && material.normalTexture.has_value()) {
            data.normalScale = material.normalTexture->scale;
            added = addTexture(material.normalTexture->textureIndex, TextureUse::Normal, data.normalImage);
        }
        if (added && material.occlusionTexture.has_value()) {
            data.occlusionStrength = material.occlusionTexture->strength;
            added = addTexture(material.occlusionTexture->textureIndex, TextureUse::Data, data.occlusionImage);
        }
        if (added && material.emissiveTexture.has_value()) {
            added = addTexture(material.emissiveTexture->textureIndex, TextureUse::Color, data.emissiveImage);
        }
        if (!added) {
            return added;
        }
        m_model.materials.push_back(std::move(data));
    }
    return {};
}

core::Result<MeshData> ModelBuilder::ReadPrimitive(const fastgltf::Primitive& primitive,
                                                   const std::string& name) const {
    const auto position = primitive.findAttribute("POSITION");
    if (position == primitive.attributes.end()) {
        return Invalid(std::format("{} has no positions", name));
    }

    MeshData mesh;
    const fastgltf::Accessor& positions = m_asset.accessors[position->accessorIndex];
    mesh.vertices.resize(positions.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(
        m_asset, positions, [&mesh](glm::vec3 value, std::size_t index) { mesh.vertices[index].position = value; });

    const auto readAttribute = [&]<class T>(std::string_view attribute, auto store) {
        const auto found = primitive.findAttribute(attribute);
        if (found == primitive.attributes.end()) {
            return false;
        }
        const fastgltf::Accessor& accessor = m_asset.accessors[found->accessorIndex];
        if (accessor.count != mesh.vertices.size()) {
            return false;
        }
        fastgltf::iterateAccessorWithIndex<T>(m_asset, accessor,
                                              [&](T value, std::size_t index) { store(mesh.vertices[index], value); });
        return true;
    };
    const bool hasNormals = readAttribute.template operator()<glm::vec3>(
        "NORMAL", [](Vertex& vertex, glm::vec3 value) { vertex.normal = value; });
    readAttribute.template operator()<glm::vec4>("TANGENT",
                                                 [](Vertex& vertex, glm::vec4 value) { vertex.tangent = value; });
    readAttribute.template operator()<glm::vec2>("TEXCOORD_0",
                                                 [](Vertex& vertex, glm::vec2 value) { vertex.uv = value; });

    const auto joints = primitive.findAttribute("JOINTS_0");
    const auto weights = primitive.findAttribute("WEIGHTS_0");
    if (joints != primitive.attributes.end() && weights != primitive.attributes.end()) {
        const fastgltf::Accessor& jointAccessor = m_asset.accessors[joints->accessorIndex];
        const fastgltf::Accessor& weightAccessor = m_asset.accessors[weights->accessorIndex];
        if (jointAccessor.count != mesh.vertices.size() || weightAccessor.count != mesh.vertices.size()) {
            return Invalid(std::format("{} has joints or weights for a different number of vertices", name));
        }
        mesh.skin.resize(mesh.vertices.size());
        bool inRange = true;
        fastgltf::iterateAccessorWithIndex<glm::uvec4>(
            m_asset, jointAccessor, [&mesh, &inRange](glm::uvec4 value, std::size_t index) {
                for (glm::length_t joint = 0; joint < 4; ++joint) {
                    inRange = inRange && value[joint] <= std::numeric_limits<std::uint16_t>::max();
                    mesh.skin[index].joints[static_cast<std::size_t>(joint)] = static_cast<std::uint16_t>(value[joint]);
                }
            });
        if (!inRange) {
            return Invalid(std::format("{} has a joint index out of range", name));
        }
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            m_asset, weightAccessor, [&mesh](glm::vec4 value, std::size_t index) {
                // Exporters round, so the sum is made 1 again.
                const float sum = value.x + value.y + value.z + value.w;
                mesh.skin[index].weights = sum > 0.0f ? value / sum : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            });
    }

    if (!primitive.indicesAccessor.has_value()) {
        return Invalid(std::format("{} has no indices", name));
    }
    const fastgltf::Accessor& indices = m_asset.accessors[*primitive.indicesAccessor];
    mesh.indices.resize(indices.count);
    fastgltf::copyFromAccessor<std::uint32_t>(m_asset, indices, mesh.indices.data());
    for (const std::uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            return Invalid(std::format("{} has an index out of range", name));
        }
    }
    if (mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
        return Invalid(std::format("{} does not hold whole triangles", name));
    }

    if (!hasNormals) {
        for (Vertex& vertex : mesh.vertices) {
            vertex.normal = glm::vec3(0.0f);
        }
        for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
            Vertex& a = mesh.vertices[mesh.indices[index]];
            Vertex& b = mesh.vertices[mesh.indices[index + 1]];
            Vertex& c = mesh.vertices[mesh.indices[index + 2]];
            // Not normalized: larger triangles weigh more.
            const glm::vec3 face = glm::cross(b.position - a.position, c.position - a.position);
            a.normal += face;
            b.normal += face;
            c.normal += face;
        }
        for (Vertex& vertex : mesh.vertices) {
            const float length = glm::length(vertex.normal);
            vertex.normal = (length > 0.0f) ? vertex.normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
    return mesh;
}

core::Result<void> ModelBuilder::AddMeshes() {
    m_meshPrimitives.resize(m_asset.meshes.size());
    for (std::size_t meshIndex = 0; meshIndex < m_asset.meshes.size(); ++meshIndex) {
        const fastgltf::Mesh& mesh = m_asset.meshes[meshIndex];
        for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
            const fastgltf::Primitive& primitive = mesh.primitives[primitiveIndex];
            const std::string name = std::format("mesh {} primitive {}", meshIndex, primitiveIndex);
            if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                DAI_LOG_WARNING(kModule, "Model '{}': skipping {}, which is not made of triangles", m_fileName, name);
                continue;
            }
            DAI_TRY_ASSIGN(MeshData data, ReadPrimitive(primitive, name));

            ModelPrimitive added;
            added.mesh = std::move(data);
            if (primitive.materialIndex.has_value()) {
                added.material = static_cast<std::uint32_t>(*primitive.materialIndex);
            }
            // glTF asks for MikkTSpace tangents when a normal-mapped primitive comes without them.
            const bool normalMapped = added.material.has_value() && *added.material < m_model.materials.size() &&
                                      m_model.materials[*added.material].normalImage.has_value();
            const bool hasTangents = primitive.findAttribute("TANGENT") != primitive.attributes.end();
            if (normalMapped && !hasTangents) {
                GenerateTangents(added.mesh);
            }
            m_meshPrimitives[meshIndex].push_back(static_cast<std::uint32_t>(m_model.primitives.size()));
            m_model.primitives.push_back(std::move(added));
        }
    }
    return {};
}

core::Result<void> ModelBuilder::AddNode(std::size_t index, std::optional<std::uint32_t> parent, std::size_t depth) {
    // Deeper than the number of nodes means that a node is its own ancestor.
    if (depth > m_asset.nodes.size()) {
        return Invalid("its node hierarchy has a cycle");
    }
    const fastgltf::Node& node = m_asset.nodes[index];

    ModelNode added;
    added.name = node.name;
    added.parent = parent;
    if (const auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
        added.local.translation = {trs->translation[0], trs->translation[1], trs->translation[2]};
        added.local.rotation = glm::quat(trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]);
        added.local.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
    }
    if (node.meshIndex.has_value()) {
        added.primitives = m_meshPrimitives[*node.meshIndex];
    }
    if (node.skinIndex.has_value()) {
        // The model keeps every skin of the file, in the same order.
        added.skin = static_cast<std::uint32_t>(*node.skinIndex);
    }
    if (node.lightIndex.has_value()) {
        // And every light.
        added.light = static_cast<std::uint32_t>(*node.lightIndex);
    }
    const auto addedIndex = static_cast<std::uint32_t>(m_model.nodes.size());
    if (m_nodeIndices[index].has_value()) {
        return Invalid("a node appears twice in its scene");
    }
    m_nodeIndices[index] = addedIndex;
    m_model.nodes.push_back(std::move(added));

    for (const std::size_t child : node.children) {
        if (core::Result<void> result = AddNode(child, addedIndex, depth + 1); !result) {
            return result;
        }
    }
    return {};
}

core::Result<void> ModelBuilder::AddSkins() {
    m_model.skins.reserve(m_asset.skins.size());
    for (std::size_t skinIndex = 0; skinIndex < m_asset.skins.size(); ++skinIndex) {
        const fastgltf::Skin& skin = m_asset.skins[skinIndex];
        SkinData data;
        data.name = skin.name;
        for (const std::size_t joint : skin.joints) {
            if (joint >= m_nodeIndices.size() || !m_nodeIndices[joint].has_value()) {
                return Invalid(std::format("skin {} has a joint outside of the scene", skinIndex));
            }
            data.joints.push_back(*m_nodeIndices[joint]);
        }
        if (data.joints.empty()) {
            return Invalid(std::format("skin {} has no joints", skinIndex));
        }
        // Without matrices, every joint is at the origin of the mesh in the bind pose.
        data.inverseBindMatrices.assign(data.joints.size(), glm::mat4(1.0f));
        if (skin.inverseBindMatrices.has_value()) {
            const fastgltf::Accessor& accessor = m_asset.accessors[*skin.inverseBindMatrices];
            if (accessor.count != data.joints.size() || accessor.type != fastgltf::AccessorType::Mat4) {
                return Invalid(std::format("skin {} does not have one matrix per joint", skinIndex));
            }
            fastgltf::iterateAccessorWithIndex<glm::mat4>(
                m_asset, accessor,
                [&data](const glm::mat4& value, std::size_t index) { data.inverseBindMatrices[index] = value; });
        }
        m_model.skins.push_back(std::move(data));
    }

    for (const ModelNode& node : m_model.nodes) {
        if (!node.skin.has_value()) {
            continue;
        }
        if (*node.skin >= m_model.skins.size()) {
            return Invalid(std::format("node '{}' has a skin that does not exist", node.name));
        }
        const std::size_t jointCount = m_model.skins[*node.skin].joints.size();
        const auto outOfRange = [jointCount](const SkinWeights& weights) {
            return std::ranges::any_of(weights.joints,
                                       [jointCount](std::uint16_t joint) { return joint >= jointCount; });
        };
        for (const std::uint32_t primitive : node.primitives) {
            if (std::ranges::any_of(m_model.primitives[primitive].mesh.skin, outOfRange)) {
                return Invalid(std::format("node '{}' uses a joint that its skin does not have", node.name));
            }
        }
    }
    return {};
}

void ModelBuilder::AddLights() {
    m_model.lights.reserve(m_asset.lights.size());
    for (const fastgltf::Light& light : m_asset.lights) {
        LightSource source;
        source.name = light.name;
        source.kind = light.type == fastgltf::LightType::Directional ? LightKind::Directional
                      : light.type == fastgltf::LightType::Spot      ? LightKind::Spot
                                                                     : LightKind::Point;
        source.color = {light.color[0], light.color[1], light.color[2]};
        source.intensity = light.intensity;
        if (light.range.has_value()) {
            source.range = *light.range;
        }
        // glTF's defaults: a cone of 45 degrees, lit fully to its axis only.
        source.innerConeAngle = light.innerConeAngle.value_or(0.0f);
        source.outerConeAngle = light.outerConeAngle.value_or(0.785398f);
        m_model.lights.push_back(std::move(source));
    }
}

core::Result<void> ModelBuilder::AddAnimations() {
    bool warnedCubic = false;
    m_model.animations.reserve(m_asset.animations.size());
    for (std::size_t animationIndex = 0; animationIndex < m_asset.animations.size(); ++animationIndex) {
        const fastgltf::Animation& animation = m_asset.animations[animationIndex];
        AnimationData data;
        data.name = animation.name.empty() ? std::format("animation {}", animationIndex) : std::string(animation.name);
        for (const fastgltf::AnimationChannel& channel : animation.channels) {
            AnimationChannel added;
            switch (channel.path) {
            case fastgltf::AnimationPath::Translation:
                added.path = AnimationPath::Translation;
                break;
            case fastgltf::AnimationPath::Rotation:
                added.path = AnimationPath::Rotation;
                break;
            case fastgltf::AnimationPath::Scale:
                added.path = AnimationPath::Scale;
                break;
            default:
                continue; // Morph target weights.
            }
            if (!channel.nodeIndex.has_value() || *channel.nodeIndex >= m_nodeIndices.size() ||
                !m_nodeIndices[*channel.nodeIndex].has_value()) {
                continue; // A node outside of the scene moves nothing that is drawn.
            }
            added.node = *m_nodeIndices[*channel.nodeIndex];

            if (channel.samplerIndex >= animation.samplers.size()) {
                return Invalid(std::format("animation '{}' has a channel without a sampler", data.name));
            }
            const fastgltf::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
            std::size_t stride = 1;
            switch (sampler.interpolation) {
            case fastgltf::AnimationInterpolation::Step:
                added.interpolation = AnimationInterpolation::Step;
                break;
            case fastgltf::AnimationInterpolation::Linear:
                added.interpolation = AnimationInterpolation::Linear;
                break;
            case fastgltf::AnimationInterpolation::CubicSpline:
                // Each key holds an in-tangent, a value and an out-tangent: only the value is kept.
                added.interpolation = AnimationInterpolation::Linear;
                stride = 3;
                if (!warnedCubic) {
                    warnedCubic = true;
                    DAI_LOG_WARNING(kModule, "Model '{}': cubic spline keys are interpolated linearly", m_fileName);
                }
                break;
            }

            const fastgltf::Accessor& input = m_asset.accessors[sampler.inputAccessor];
            const fastgltf::Accessor& output = m_asset.accessors[sampler.outputAccessor];
            if (input.count == 0 || output.count != input.count * stride) {
                return Invalid(std::format("animation '{}' has a sampler with mismatched keys", data.name));
            }
            added.times.resize(input.count);
            fastgltf::copyFromAccessor<float>(m_asset, input, added.times.data());
            if (!std::ranges::is_sorted(added.times)) {
                return Invalid(std::format("animation '{}' has keys out of order", data.name));
            }
            std::vector<glm::vec4> values(output.count);
            if (added.path == AnimationPath::Rotation) {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    m_asset, output, [&values](glm::vec4 value, std::size_t index) { values[index] = value; });
            } else {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    m_asset, output, [&values](glm::vec3 value, std::size_t index) { values[index] = {value, 0.0f}; });
            }
            added.values.reserve(input.count);
            for (std::size_t key = 0; key < input.count; ++key) {
                added.values.push_back(values[key * stride + (stride == 3 ? 1 : 0)]);
            }
            data.channels.push_back(std::move(added));
        }
        m_model.animations.push_back(std::move(data));
    }
    return {};
}

core::Result<ModelData> ModelBuilder::Build() {
    DAI_TRY(AddMaterials());
    DAI_TRY(AddMeshes());
    if (m_asset.scenes.empty()) {
        return Invalid("it has no scene");
    }
    const fastgltf::Scene& scene = m_asset.scenes[m_asset.defaultScene.value_or(0)];
    m_nodeIndices.assign(m_asset.nodes.size(), std::nullopt);
    for (const std::size_t root : scene.nodeIndices) {
        DAI_TRY(AddNode(root, std::nullopt, 0));
    }
    DAI_TRY(AddSkins());
    DAI_TRY(AddAnimations());
    AddLights();
    return std::move(m_model);
}

} // namespace

core::Result<ModelData> LoadModelFile(const std::filesystem::path& path) {
    DAI_PROFILE_SCOPE("resources::LoadModelFile");
    const std::string fileName = ToUtf8(path);
    if (!std::filesystem::is_regular_file(path)) {
        return core::MakeError(core::ErrorCode::NotFound, kModule, std::format("Model file '{}' not found", fileName));
    }

    fastgltf::GltfFileStream stream(path);
    if (!stream.isOpen()) {
        return core::MakeError(core::ErrorCode::IoError, kModule, std::format("Cannot open model file '{}'", fileName));
    }

    fastgltf::Parser parser(
        fastgltf::Extensions::KHR_texture_basisu | fastgltf::Extensions::KHR_materials_transmission |
        fastgltf::Extensions::KHR_materials_volume | fastgltf::Extensions::KHR_materials_ior |
        fastgltf::Extensions::KHR_materials_emissive_strength | fastgltf::Extensions::KHR_lights_punctual);
    Extras extras;
    parser.setUserPointer(&extras);
    parser.setExtrasParseCallback(&ReadExtras);
    constexpr fastgltf::Options kOptions = fastgltf::Options::LoadExternalBuffers |
                                           fastgltf::Options::DecomposeNodeMatrices |
                                           fastgltf::Options::GenerateMeshIndices;
    fastgltf::Expected<fastgltf::Asset> asset = parser.loadGltf(stream, path.parent_path(), kOptions);
    if (asset.error() != fastgltf::Error::None) {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("Model '{}': {}", fileName, fastgltf::getErrorMessage(asset.error())));
    }
    return ModelBuilder(asset.get(), path.parent_path(), fileName, std::move(extras)).Build();
}

} // namespace daidalos::resources
