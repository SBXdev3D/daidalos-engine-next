// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/ModelBatch.hpp>

#include <utility>

namespace daidalos::scene {

std::shared_ptr<ModelBatch> ModelBatch::Start(std::vector<std::string> paths, const Loader& load, Done done) {
    auto batch = std::make_shared<ModelBatch>();
    batch->m_paths = std::move(paths);
    batch->m_done = std::move(done);
    if (batch->m_paths.empty()) {
        if (batch->m_done) {
            batch->m_done(*batch);
        }
        return batch;
    }
    // A load may call back at once, so the paths are copied: the batch must not change under the loop.
    const std::vector<std::string> queued = batch->m_paths;
    for (const std::string& path : queued) {
        load(path, [batch, path](const core::Result<const Model*>& model) { batch->Arrived(path, model); });
    }
    return batch;
}

std::shared_ptr<ModelBatch> ModelBatch::Start(AssetLibrary& assets, core::WorkerPool& workers,
                                              std::vector<std::string> paths, Done done) {
    return Start(
        std::move(paths),
        [&assets, &workers](const std::string& path, std::function<void(const core::Result<const Model*>&)> arrived) {
            assets.LoadModelAsync(path, workers,
                                  [arrived = std::move(arrived)](core::Result<const Model*> model) { arrived(model); });
        },
        std::move(done));
}

float ModelBatch::Fraction() const {
    return m_paths.empty() ? 1.0f : static_cast<float>(m_loaded) / static_cast<float>(m_paths.size());
}

void ModelBatch::Arrived(const std::string& path, const core::Result<const Model*>& model) {
    ++m_loaded;
    m_latest = path;
    if (!model) {
        m_failures.push_back(model.error());
    }
    if (Finished() && !m_cancelled && m_done) {
        // Called once: a batch finishes once.
        Done done = std::move(m_done);
        done(*this);
    }
}

} // namespace daidalos::scene
