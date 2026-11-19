// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/rhi/CommandList.hpp>
#include <daidalos/rhi/Device.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace daidalos::renderer {

/// What a pass of the render graph uses. Names are string literals: the graph keeps views of them.
struct PassDesc {
    std::string_view name;
    /// The pass's line in the GPU times and its outer debug label. Passes in a row with the same group
    /// share them. Empty: the pass's name.
    std::string_view group;
    /// Begun before the pass records and ended after. Its targets need not be listed in textures: they
    /// are written, and a target that is cleared or receives a resolve loses its content first.
    std::optional<rhi::RenderingDesc> rendering;
    std::vector<rhi::TextureUse> textures;
    std::vector<rhi::BufferUse> buffers;
    std::vector<rhi::AccelerationStructureUse> structures;
    /// May run on the compute queue, beside the graphics passes after it that use none of what it writes and
    /// write none of what it reads (ADR-0095). Only compute work: no rendering.
    bool async = false;
};

using PassRecorder = std::function<void(rhi::CommandList&)>;

/// A pass ready to run: every use, the targets of its rendering included, one per resource.
struct PlannedPass {
    std::vector<rhi::TextureUse> textures;
    std::vector<rhi::BufferUse> buffers;
    std::vector<rhi::AccelerationStructureUse> structures;
    std::string_view group;
    /// Its group differs from the previous pass's: a timestamp and a label start here.
    bool startsGroup = false;
};

/// Which passes run on the compute queue (ADR-0095). The compute queue starts after the passes before fork,
/// and the passes from join on wait for it; the graphics passes between them run beside it.
struct AsyncPlan {
    std::size_t fork = 0;
    std::size_t join = 0;
    /// Per pass: whether it runs on the compute queue. All false when nothing does.
    std::vector<bool> onCompute;
};

/// The frame's passes, in the order they run (ADR-0032). Each declares what it reads and writes, and
/// the graph places the barriers, wraps each pass in debug labels and times each group of passes. It
/// does not reorder passes or own resources: passes use the renderer's textures and buffers. Rebuilt
/// every frame.
class RenderGraph {
public:
    void AddPass(PassDesc desc, PassRecorder record);

    /// The uses of each pass, in order. Uses of the same resource in one pass are merged: their
    /// accesses combine, and its content is dropped only if every use says so.
    [[nodiscard]] std::vector<PlannedPass> Plan() const;

    /// Records every pass in order, each after its barriers, and empties the graph. Writes a timestamp
    /// before the first group and after each group. Returns the groups, in the order of their times.
    std::vector<std::string_view> Execute(rhi::CommandList& commands);
    /// The same, with async passes on the device's compute queue when it has one (ADR-0095). computeGroups
    /// receives the compute queue's groups, in the order of its timestamps: one before its first group and
    /// one after each.
    std::vector<std::string_view> Execute(rhi::CommandList& commands, rhi::Device& device,
                                          std::vector<std::string_view>& computeGroups);

    /// Fork at the first async pass. Join at the first graphics pass after it that writes what an async pass
    /// uses, uses what one writes, or uses a texture one uses (its layout is shared). Async passes from the
    /// join on run on the graphics queue, in order.
    [[nodiscard]] static AsyncPlan PlanAsync(const std::vector<PlannedPass>& planned, const std::vector<bool>& async);

    [[nodiscard]] std::size_t PassCount() const noexcept { return m_passes.size(); }

private:
    struct Pass {
        PassDesc desc;
        PassRecorder record;
    };

    void Record(rhi::CommandList& commands, std::size_t index, const PlannedPass& step,
                std::vector<std::string_view>& groups, std::string_view& group);

    std::vector<Pass> m_passes;
};

} // namespace daidalos::renderer
