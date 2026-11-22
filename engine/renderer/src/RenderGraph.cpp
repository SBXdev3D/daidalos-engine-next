// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "RenderGraph.hpp"

#include <daidalos/core/Error.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>

#include <algorithm>
#include <utility>

namespace daidalos::renderer {

namespace {

void AddTexture(std::vector<rhi::TextureUse>& uses, const rhi::TextureUse& use) {
    if (!use.texture) {
        return;
    }
    const auto found = std::ranges::find(uses, use.texture, &rhi::TextureUse::texture);
    if (found == uses.end()) {
        uses.push_back(use);
        return;
    }
    found->access = found->access | use.access;
    found->discard = found->discard && use.discard;
}

void AddBuffer(std::vector<rhi::BufferUse>& uses, const rhi::BufferUse& use) {
    if (!use.buffer) {
        return;
    }
    const auto found = std::ranges::find(uses, use.buffer, &rhi::BufferUse::buffer);
    if (found == uses.end()) {
        uses.push_back(use);
        return;
    }
    found->access = found->access | use.access;
}

void AddStructure(std::vector<rhi::AccelerationStructureUse>& uses, const rhi::AccelerationStructureUse& use) {
    if (!use.structure) {
        return;
    }
    const auto found = std::ranges::find(uses, use.structure, &rhi::AccelerationStructureUse::structure);
    if (found == uses.end()) {
        uses.push_back(use);
        return;
    }
    found->access = found->access | use.access;
}

} // namespace

void RenderGraph::AddPass(PassDesc desc, PassRecorder record) {
    m_passes.push_back({std::move(desc), std::move(record)});
}

std::vector<PlannedPass> RenderGraph::Plan() const {
    std::vector<PlannedPass> planned;
    planned.reserve(m_passes.size());
    std::string_view previousGroup;
    for (const Pass& pass : m_passes) {
        const PassDesc& desc = pass.desc;
        PlannedPass& step = planned.emplace_back();
        if (desc.rendering.has_value()) {
            const rhi::RenderingDesc& rendering = *desc.rendering;
            AddTexture(step.textures,
                       {rendering.colorTarget, rhi::Access::ColorTarget, rendering.clearColor.has_value()});
            AddTexture(step.textures,
                       {rendering.depthTarget, rhi::Access::DepthTarget, rendering.clearDepth.has_value()});
            for (const rhi::ColorTarget& target : rendering.moreColorTargets) {
                AddTexture(step.textures, {target.texture, rhi::Access::ColorTarget, target.clear.has_value()});
            }
        }
        for (const rhi::TextureUse& use : desc.textures) {
            AddTexture(step.textures, use);
        }
        for (const rhi::BufferUse& use : desc.buffers) {
            AddBuffer(step.buffers, use);
        }
        for (const rhi::AccelerationStructureUse& use : desc.structures) {
            AddStructure(step.structures, use);
        }
        step.group = desc.group.empty() ? desc.name : desc.group;
        step.startsGroup = planned.size() == 1 || step.group != previousGroup;
        previousGroup = step.group;
    }
    return planned;
}

namespace {

bool SharesTexture(const PlannedPass& a, const PlannedPass& b) {
    return std::ranges::any_of(a.textures, [&](const rhi::TextureUse& use) {
        return std::ranges::find(b.textures, use.texture, &rhi::TextureUse::texture) != b.textures.end();
    });
}

// Whether one pass writes what the other uses, of the resources both name.
template <typename Use, typename Handle>
bool Conflicts(const std::vector<Use>& a, const std::vector<Use>& b, Handle Use::* handle) {
    for (const Use& use : a) {
        const auto found = std::ranges::find(b, use.*handle, handle);
        if (found != b.end() && (rhi::Writes(use.access) || rhi::Writes(found->access))) {
            return true;
        }
    }
    return false;
}

bool Conflicts(const PlannedPass& a, const PlannedPass& b) {
    return SharesTexture(a, b) || Conflicts(a.buffers, b.buffers, &rhi::BufferUse::buffer) ||
           Conflicts(a.structures, b.structures, &rhi::AccelerationStructureUse::structure);
}

// Closes the open group of a list: its label, and the timestamp that ends its time.
void CloseGroup(rhi::CommandList& commands, std::string_view& group) {
    if (!group.empty()) {
        commands.EndLabel();
        commands.WriteTimestamp();
        group = {};
    }
}

} // namespace

AsyncPlan RenderGraph::PlanAsync(const std::vector<PlannedPass>& planned, const std::vector<bool>& async) {
    AsyncPlan plan{.fork = planned.size(), .join = planned.size(), .onCompute = std::vector<bool>(planned.size())};
    const auto first = std::ranges::find(async, true);
    if (first == async.end()) {
        return plan;
    }
    plan.fork = static_cast<std::size_t>(first - async.begin());
    std::vector<std::size_t> computed;
    for (std::size_t index = plan.fork; index < planned.size(); ++index) {
        if (async[index]) {
            plan.onCompute[index] = true;
            computed.push_back(index);
            continue;
        }
        // A graphics pass that depends on an async pass, or that one depends on, waits for the compute queue.
        const bool conflicts =
            std::ranges::any_of(computed, [&](std::size_t other) { return Conflicts(planned[index], planned[other]); });
        // An async pass after it that depends on it could not start before it either.
        bool later = false;
        for (std::size_t other = index + 1; other < planned.size() && !later; ++other) {
            later = async[other] && Conflicts(planned[index], planned[other]);
        }
        if (conflicts || later) {
            plan.join = index;
            break;
        }
    }
    // Async passes after the join wait for nothing on the compute queue: they run on the graphics queue.
    for (std::size_t index = plan.join; index < planned.size(); ++index) {
        plan.onCompute[index] = false;
    }
    return plan;
}

void RenderGraph::Record(rhi::CommandList& commands, std::size_t index, const PlannedPass& step,
                         std::vector<std::string_view>& groups, std::string_view& group) {
    const PassDesc& desc = m_passes[index].desc;
    if (step.group != group) {
        CloseGroup(commands, group);
        groups.push_back(step.group);
        commands.BeginLabel(step.group);
        group = step.group;
    }
    const bool ownLabel = desc.name != step.group;
    if (ownLabel) {
        commands.BeginLabel(desc.name);
    }
    commands.Use(step.textures, step.buffers, step.structures);
    if (desc.rendering.has_value()) {
        commands.BeginRendering(*desc.rendering);
    }
    m_passes[index].record(commands);
    if (desc.rendering.has_value()) {
        commands.EndRendering();
    }
    if (ownLabel) {
        commands.EndLabel();
    }
}

std::vector<std::string_view> RenderGraph::Execute(rhi::CommandList& commands) {
    DAI_PROFILE_SCOPE("renderer::RenderGraph::Execute");
    const std::vector<PlannedPass> planned = Plan();
    std::vector<std::string_view> groups;
    std::string_view group;
    commands.WriteTimestamp();
    for (std::size_t index = 0; index < m_passes.size(); ++index) {
        Record(commands, index, planned[index], groups, group);
    }
    CloseGroup(commands, group);
    m_passes.clear();
    return groups;
}

std::vector<std::string_view> RenderGraph::Execute(rhi::CommandList& commands, rhi::Device& device,
                                                   std::vector<std::string_view>& computeGroups) {
    DAI_PROFILE_SCOPE("renderer::RenderGraph::Execute");
    computeGroups.clear();
    std::vector<bool> async(m_passes.size());
    for (std::size_t index = 0; index < m_passes.size(); ++index) {
        async[index] = m_passes[index].desc.async;
    }
    if (!device.HasAsyncCompute() || std::ranges::find(async, true) == async.end()) {
        return Execute(commands);
    }
    const std::vector<PlannedPass> planned = Plan();
    const AsyncPlan plan = PlanAsync(planned, async);
    std::vector<std::string_view> groups;
    std::string_view group;
    commands.WriteTimestamp();
    for (std::size_t index = 0; index < plan.fork; ++index) {
        Record(commands, index, planned[index], groups, group);
    }
    CloseGroup(commands, group);
    core::Result<rhi::CommandList> compute = device.BeginAsyncCompute();
    if (!compute) {
        // The device is failing: the rest of the frame runs in order, and the next submission reports it.
        DAI_LOG_ERROR("renderer", "The compute queue's commands could not begin: {}", core::Format(compute.error()));
        for (std::size_t index = plan.fork; index < m_passes.size(); ++index) {
            Record(commands, index, planned[index], groups, group);
        }
        CloseGroup(commands, group);
        m_passes.clear();
        return groups;
    }
    // The graphics passes beside the compute queue are recorded first: the device tracks each resource's last
    // use in the order uses are recorded, and these must place their own barriers on the graphics queue.
    for (std::size_t index = plan.fork; index < plan.join; ++index) {
        if (!plan.onCompute[index]) {
            Record(commands, index, planned[index], groups, group);
        }
    }
    CloseGroup(commands, group);
    std::string_view computeGroup;
    compute->WriteTimestamp();
    for (std::size_t index = plan.fork; index < plan.join; ++index) {
        if (plan.onCompute[index]) {
            Record(*compute, index, planned[index], computeGroups, computeGroup);
        }
    }
    CloseGroup(*compute, computeGroup);
    if (const core::Result<void> joined = device.JoinAsyncCompute(); !joined) {
        DAI_LOG_ERROR("renderer", "The compute queue's commands could not end: {}", core::Format(joined.error()));
    }
    // The time the graphics queue waits for the compute queue, if it does.
    groups.emplace_back("Waiting for the compute queue");
    commands.WriteTimestamp();
    for (std::size_t index = plan.join; index < m_passes.size(); ++index) {
        Record(commands, index, planned[index], groups, group);
    }
    CloseGroup(commands, group);
    m_passes.clear();
    return groups;
}

} // namespace daidalos::renderer
