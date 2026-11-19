// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/ModelBatch.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace daidalos;
using scene::ModelBatch;

namespace {

/// Loads that wait until the test delivers them, as workers would.
struct DelayedLoads {
    std::vector<std::pair<std::string, std::function<void(const core::Result<const scene::Model*>&)>>> waiting;

    [[nodiscard]] ModelBatch::Loader Loader() {
        return [this](const std::string& path, std::function<void(const core::Result<const scene::Model*>&)> done) {
            waiting.emplace_back(path, std::move(done));
        };
    }

    void Deliver(std::size_t index, bool succeed = true) {
        const core::Result<const scene::Model*> model =
            succeed ? core::Result<const scene::Model*>(nullptr)
                    : core::Result<const scene::Model*>(
                          tl::unexpected(core::MakeError(core::ErrorCode::NotFound, "test", waiting[index].first)));
        waiting[index].second(model);
    }
};

} // namespace

TEST(ModelBatchTest, CallsDoneOnceTheLastModelIsIn) {
    DelayedLoads loads;
    int calls = 0;
    const auto batch =
        ModelBatch::Start({"a.gltf", "b.gltf", "c.gltf"}, loads.Loader(), [&calls](const ModelBatch& done) {
            ++calls;
            EXPECT_TRUE(done.Finished());
        });
    ASSERT_EQ(loads.waiting.size(), 3U);
    EXPECT_EQ(batch->Total(), 3U);
    EXPECT_FLOAT_EQ(batch->Fraction(), 0.0f);

    loads.Deliver(1);
    EXPECT_EQ(batch->Loaded(), 1U);
    EXPECT_EQ(batch->Latest(), "b.gltf");
    EXPECT_NEAR(batch->Fraction(), 1.0f / 3.0f, 1e-6f);
    EXPECT_EQ(calls, 0);

    loads.Deliver(0);
    loads.Deliver(2);
    EXPECT_EQ(calls, 1);
    EXPECT_FLOAT_EQ(batch->Fraction(), 1.0f);
}

TEST(ModelBatchTest, KeepsTheFailuresAndStillFinishes) {
    DelayedLoads loads;
    std::size_t failures = 0;
    const auto batch = ModelBatch::Start({"a.gltf", "missing.gltf"}, loads.Loader(),
                                         [&failures](const ModelBatch& done) { failures = done.Failures().size(); });
    loads.Deliver(0);
    loads.Deliver(1, false);
    EXPECT_EQ(failures, 1U);
    EXPECT_NE(batch->Failures().front().message.find("missing.gltf"), std::string::npos);
}

TEST(ModelBatchTest, NothingToLoadIsDoneAtOnce) {
    DelayedLoads loads;
    bool done = false;
    const auto batch = ModelBatch::Start({}, loads.Loader(), [&done](const ModelBatch& /*batch*/) { done = true; });
    EXPECT_TRUE(done);
    EXPECT_TRUE(batch->Finished());
    EXPECT_FLOAT_EQ(batch->Fraction(), 1.0f);
}

TEST(ModelBatchTest, LoadsThatAnswerAtOnceFinishIt) {
    // A model the library holds already answers inside the load call.
    bool done = false;
    const auto batch = ModelBatch::Start(
        {"a.gltf", "b.gltf"},
        [](const std::string& /*path*/, std::function<void(const core::Result<const scene::Model*>&)> arrived) {
            arrived(core::Result<const scene::Model*>(nullptr));
        },
        [&done](const ModelBatch& /*batch*/) { done = true; });
    EXPECT_TRUE(done);
    EXPECT_EQ(batch->Loaded(), 2U);
}

TEST(ModelBatchTest, CancelledBatchesNeverCallDone) {
    DelayedLoads loads;
    bool done = false;
    const auto batch =
        ModelBatch::Start({"a.gltf"}, loads.Loader(), [&done](const ModelBatch& /*batch*/) { done = true; });
    batch->Cancel();
    loads.Deliver(0);
    EXPECT_FALSE(done);
    EXPECT_TRUE(batch->Cancelled());
}
