// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace daidalos::core {
class WorkerPool;
}

namespace daidalos::scene {

class AssetLibrary;
struct Model;

/// The models of a level loading on workers while a loading screen shows how far they are (ADR-0105): the
/// sandbox's screen and the editor's loader both wait on one.
class ModelBatch {
public:
    /// Loads one path and calls back with the model, on the main thread, now or later.
    using Loader =
        std::function<void(const std::string& path, std::function<void(const core::Result<const Model*>&)> done)>;
    using Done = std::function<void(const ModelBatch& batch)>;

    /// Starts loading every path; done is called once, on the main thread, when the last one is in, unless the
    /// batch was cancelled first. With no paths, done is called at once.
    [[nodiscard]] static std::shared_ptr<ModelBatch> Start(std::vector<std::string> paths, const Loader& load,
                                                           Done done);
    /// The same through the library's own loads (AssetLibrary::LoadModelAsync). Nothing is called once the
    /// library is gone.
    [[nodiscard]] static std::shared_ptr<ModelBatch> Start(AssetLibrary& assets, core::WorkerPool& workers,
                                                           std::vector<std::string> paths, Done done);

    [[nodiscard]] std::size_t Total() const { return m_paths.size(); }
    [[nodiscard]] std::size_t Loaded() const { return m_loaded; }
    /// From 0 to 1; 1 with nothing to load.
    [[nodiscard]] float Fraction() const;
    [[nodiscard]] bool Finished() const { return m_loaded == m_paths.size(); }
    /// The path that came in last, empty before the first.
    [[nodiscard]] const std::string& Latest() const { return m_latest; }
    /// The models that could not be loaded, in the order they failed.
    [[nodiscard]] const std::vector<core::Error>& Failures() const { return m_failures; }

    /// The loads already started still finish, but done is not called.
    void Cancel() { m_cancelled = true; }
    [[nodiscard]] bool Cancelled() const { return m_cancelled; }

private:
    void Arrived(const std::string& path, const core::Result<const Model*>& model);

    std::vector<std::string> m_paths;
    std::size_t m_loaded = 0;
    std::string m_latest;
    std::vector<core::Error> m_failures;
    Done m_done;
    bool m_cancelled = false;
};

} // namespace daidalos::scene
