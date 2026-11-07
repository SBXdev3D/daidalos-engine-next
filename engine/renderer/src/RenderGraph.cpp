// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "RenderGraph.hpp"

#include <daidalos/core/Profile.hpp>

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
            AddTexture(step.textures, {rendering.colorResolve, rhi::Access::ColorTarget, true});
            AddTexture(step.textures, {rendering.depthResolve, rhi::Access::DepthResolve, true});
        }
        for (const rhi::TextureUse& use : desc.textures) {
            AddTexture(step.textures, use);
        }
        for (const rhi::BufferUse& use : desc.buffers) {
            AddBuffer(step.buffers, use);
        }
        step.group = desc.group.empty() ? desc.name : desc.group;
        step.startsGroup = planned.size() == 1 || step.group != previousGroup;
        previousGroup = step.group;
    }
    return planned;
}

std::vector<std::string_view> RenderGraph::Execute(rhi::CommandList& commands) {
    DAI_PROFILE_SCOPE("renderer::RenderGraph::Execute");
    const std::vector<PlannedPass> planned = Plan();
    std::vector<std::string_view> groups;
    commands.WriteTimestamp();
    for (std::size_t index = 0; index < m_passes.size(); ++index) {
        const PlannedPass& step = planned[index];
        const PassDesc& desc = m_passes[index].desc;
        if (step.startsGroup) {
            if (!groups.empty()) {
                commands.EndLabel();
                commands.WriteTimestamp();
            }
            groups.push_back(step.group);
            commands.BeginLabel(step.group);
        }
        const bool ownLabel = desc.name != step.group;
        if (ownLabel) {
            commands.BeginLabel(desc.name);
        }
        commands.Use(step.textures, step.buffers);
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
    if (!groups.empty()) {
        commands.EndLabel();
        commands.WriteTimestamp();
    }
    m_passes.clear();
    return groups;
}

} // namespace daidalos::renderer
