// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/renderer/Scene.hpp>

#include <glm/matrix.hpp>

#include <cstdint>
#include <limits>

namespace daidalos::renderer {

void ExtractScene(ecs::World& world, SceneView& view, ecs::Entity activeCamera) {
    DAI_PROFILE_SCOPE("renderer::ExtractScene");
    view.camera.reset();
    view.draws.clear();
    view.jointMatrices.clear();

    bool chosen = false;
    std::uint32_t cameraIndex = std::numeric_limits<std::uint32_t>::max();
    world.Each<ecs::WorldTransform, Camera>(
        [&](ecs::Entity entity, const ecs::WorldTransform& transform, const Camera& camera) {
            if (entity == activeCamera || (!chosen && entity.Index() < cameraIndex)) {
                chosen = (entity == activeCamera);
                cameraIndex = entity.Index();
                view.camera = CameraView{glm::inverse(transform.matrix), camera.verticalFov, camera.nearPlane};
            }
        });

    view.draws.reserve(world.Count<MeshRenderer>());
    world.Each<ecs::WorldTransform, MeshRenderer>(
        [&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform, const MeshRenderer& renderer) {
            view.draws.push_back({renderer.mesh, transform.matrix, renderer.material});
        });

    world.Each<ecs::WorldTransform, SkinnedMeshRenderer>(
        [&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform, const SkinnedMeshRenderer& renderer) {
            const auto firstJoint = static_cast<std::uint32_t>(view.jointMatrices.size());
            const std::vector<glm::mat4>* inverseBind = renderer.inverseBindMatrices.get();
            for (std::size_t index = 0; index < renderer.joints.size(); ++index) {
                const ecs::WorldTransform* joint = world.TryGet<ecs::WorldTransform>(renderer.joints[index]);
                const glm::mat4 bind =
                    (inverseBind != nullptr && index < inverseBind->size()) ? (*inverseBind)[index] : glm::mat4(1.0f);
                view.jointMatrices.push_back((joint != nullptr ? joint->matrix : transform.matrix) * bind);
            }
            view.draws.push_back(
                {renderer.mesh, transform.matrix, renderer.material, renderer.joints.empty() ? kNoJoints : firstJoint});
        });
}

} // namespace daidalos::renderer
