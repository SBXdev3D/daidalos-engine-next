// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/rhi/CommandList.hpp>

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

    [[nodiscard]] std::size_t PassCount() const noexcept { return m_passes.size(); }

private:
    struct Pass {
        PassDesc desc;
        PassRecorder record;
    };

    std::vector<Pass> m_passes;
};

} // namespace daidalos::renderer
