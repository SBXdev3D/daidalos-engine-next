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
}

} // namespace daidalos::renderer
