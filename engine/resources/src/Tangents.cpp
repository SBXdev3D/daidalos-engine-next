// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/resources/ModelData.hpp>

#include <glm/geometric.hpp>

#include <mikktspace.h>

#include <cstddef>
#include <vector>

namespace daidalos::resources {

namespace {

// What MikkTSpace reads through its callbacks, and the tangent it gives each corner of each triangle.
struct Generation {
    MeshData* mesh;
    std::vector<glm::vec4> cornerTangents;
};

const Vertex& CornerVertex(const SMikkTSpaceContext* context, int face, int corner) {
    const auto& generation = *static_cast<const Generation*>(context->m_pUserData);
    const auto index = static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(corner);
    return generation.mesh->vertices[generation.mesh->indices[index]];
}

int FaceCount(const SMikkTSpaceContext* context) {
    return static_cast<int>(static_cast<const Generation*>(context->m_pUserData)->mesh->indices.size() / 3);
}

int CornerCount(const SMikkTSpaceContext* /*context*/, int /*face*/) {
    return 3;
}

void Position(const SMikkTSpaceContext* context, float out[], int face, int corner) {
    const glm::vec3& position = CornerVertex(context, face, corner).position;
    out[0] = position.x;
    out[1] = position.y;
    out[2] = position.z;
}

void Normal(const SMikkTSpaceContext* context, float out[], int face, int corner) {
    const glm::vec3& normal = CornerVertex(context, face, corner).normal;
    out[0] = normal.x;
    out[1] = normal.y;
    out[2] = normal.z;
}

void TexCoord(const SMikkTSpaceContext* context, float out[], int face, int corner) {
    // MikkTSpace expects v to point up, as in OpenGL, and glTF's v points down.
    const glm::vec2& uv = CornerVertex(context, face, corner).uv;
    out[0] = uv.x;
    out[1] = 1.0f - uv.y;
}

void SetTangent(const SMikkTSpaceContext* context, const float tangent[], float sign, int face, int corner) {
    auto& generation = *static_cast<Generation*>(context->m_pUserData);
    const auto index = static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(corner);
    generation.cornerTangents[index] = glm::vec4(tangent[0], tangent[1], tangent[2], sign);
}

bool SameTangent(const glm::vec4& a, const glm::vec4& b) {
    return a.w == b.w && glm::dot(glm::vec3(a), glm::vec3(b)) > 0.9999f;
}

} // namespace

void GenerateTangents(MeshData& mesh) {
    DAI_PROFILE_FUNCTION();
    if (mesh.indices.empty()) {
        return;
    }
    Generation generation{&mesh, std::vector<glm::vec4>(mesh.indices.size())};
    SMikkTSpaceInterface callbacks{};
    callbacks.m_getNumFaces = FaceCount;
    callbacks.m_getNumVerticesOfFace = CornerCount;
    callbacks.m_getPosition = Position;
    callbacks.m_getNormal = Normal;
    callbacks.m_getTexCoord = TexCoord;
    callbacks.m_setTSpaceBasic = SetTangent;
    SMikkTSpaceContext context{};
    context.m_pInterface = &callbacks;
    context.m_pUserData = &generation;
    genTangSpaceDefault(&context);

    // MikkTSpace gives a tangent per corner. A vertex shared by corners that disagree, along a UV seam
    // or a mirrored half, is split into one copy per tangent.
    const std::size_t originalCount = mesh.vertices.size();
    std::vector<bool> assigned(originalCount, false);
    // For each original vertex, the copies made of it, as a linked list through the new vertices.
    std::vector<std::uint32_t> firstCopy(originalCount, UINT32_MAX);
    std::vector<std::uint32_t> nextCopy;
    for (std::size_t corner = 0; corner < mesh.indices.size(); ++corner) {
        const std::uint32_t original = mesh.indices[corner];
        const glm::vec4& tangent = generation.cornerTangents[corner];
        if (!assigned[original]) {
            mesh.vertices[original].tangent = tangent;
            assigned[original] = true;
            continue;
        }
        if (SameTangent(mesh.vertices[original].tangent, tangent)) {
            continue;
        }
        std::uint32_t copy = firstCopy[original];
        while (copy != UINT32_MAX && !SameTangent(mesh.vertices[copy].tangent, tangent)) {
            copy = nextCopy[copy - originalCount];
        }
        if (copy == UINT32_MAX) {
            copy = static_cast<std::uint32_t>(mesh.vertices.size());
            Vertex split = mesh.vertices[original];
            split.tangent = tangent;
            mesh.vertices.push_back(split);
            if (!mesh.skin.empty()) {
                mesh.skin.push_back(mesh.skin[original]);
            }
            nextCopy.push_back(firstCopy[original]);
            firstCopy[original] = copy;
        }
        mesh.indices[corner] = copy;
    }
}

} // namespace daidalos::resources
