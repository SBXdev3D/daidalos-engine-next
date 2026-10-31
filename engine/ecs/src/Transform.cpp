// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/ecs/World.hpp>

#include <vector>

namespace daidalos::ecs {

void PropagateTransforms(World& world) {
    DAI_PROFILE_FUNCTION();

    struct Pending {
        Entity entity;
        glm::mat4 parentWorld;
    };
    // Top-down, depth first, with an explicit stack so that deep hierarchies cannot overflow the call stack.
    std::vector<Pending> stack;

    world.Each<LocalTransform>([&](Entity root, LocalTransform&) {
        const Entity parent = world.Parent(root);
        if (!parent.IsNull() && world.Has<LocalTransform>(parent)) {
            return; // Reached from its root instead.
        }

        stack.push_back(Pending{root, glm::mat4(1.0f)});
        while (!stack.empty()) {
            const Pending current = stack.back();
            stack.pop_back();

            const glm::mat4 matrix = current.parentWorld * math::ToMatrix(world.Get<LocalTransform>(current.entity));
            if (WorldTransform* existing = world.TryGet<WorldTransform>(current.entity)) {
                existing->matrix = matrix;
            } else {
                world.Add<WorldTransform>(current.entity, matrix);
            }

            for (Entity child = world.FirstChild(current.entity); !child.IsNull(); child = world.NextSibling(child)) {
                if (world.Has<LocalTransform>(child)) {
                    stack.push_back(Pending{child, matrix});
                }
            }
        }
    });
}

} // namespace daidalos::ecs
