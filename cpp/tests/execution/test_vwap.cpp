#include "quant_engine/execution/twap.hpp"
#include "quant_engine/execution/vwap.hpp"

#include "../test_support.hpp"

#include <chrono>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace {
using namespace quant_engine;
using namespace quant_engine::execution;

Timestamp minute(const int value) {
    return Timestamp{} + std::chrono::minutes{value};
}

ParentOrder parent(const Quantity quantity) {
    return ParentOrder{1, "AAPL", Side::Buy, quantity, minute(0), minute(40)};
}

VolumeProfile profile(const std::vector<double>& weights) {
    std::vector<VolumeBucket> buckets;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        const int start = static_cast<int>(index) * 10;
        buckets.push_back(VolumeBucket{minute(start), minute(start + 10), weights[index]});
    }
    return VolumeProfile{std::move(buckets)};
}

Quantity total(const std::vector<ChildOrder>& schedule) {
    return std::accumulate(
        schedule.begin(), schedule.end(), Quantity{0},
        [](const Quantity value, const ChildOrder& child) {
            return value + child.quantity;
        });
}

void simple_uniform_profile_allocates_evenly() {
    const auto schedule = VWAP({profile({1, 1, 1, 1})}).generate_schedule(parent(1'000), {});
    CHECK(schedule.size() == 4);
    for (const ChildOrder& child : schedule) {
        CHECK(child.quantity == 250);
    }
}

void non_uniform_profile_matches_expected_weights() {
    const auto schedule =
        VWAP({profile({0.10, 0.20, 0.30, 0.40})}).generate_schedule(parent(1'000), {});
    CHECK(schedule[0].quantity == 100);
    CHECK(schedule[1].quantity == 200);
    CHECK(schedule[2].quantity == 300);
    CHECK(schedule[3].quantity == 400);
}

void profile_weights_are_normalized() {
    const VolumeProfile normalized = profile({10, 20, 30, 40});
    double sum = 0.0;
    for (const VolumeBucket& bucket : normalized.buckets()) {
        sum += bucket.weight;
    }
    CHECK(std::abs(sum - 1.0) < 1e-12);
    CHECK(std::abs(normalized.buckets()[0].weight - 0.10) < 1e-12);
}

void largest_remainder_rounding_is_deterministic() {
    const auto schedule =
        VWAP({profile({0.33, 0.33, 0.34})}).generate_schedule(
            ParentOrder{1, "AAPL", Side::Buy, 101, minute(0), minute(40)}, {});
    CHECK(schedule.size() == 3);
    CHECK(schedule[0].quantity == 33);
    CHECK(schedule[1].quantity == 33);
    CHECK(schedule[2].quantity == 35);
    CHECK(total(schedule) == 101);
}

void quantity_invariant_holds_after_rounding() {
    for (const Quantity quantity : {Quantity{7}, Quantity{101}, Quantity{10'003}}) {
        const auto schedule =
            VWAP({profile({0.17, 0.23, 0.60})}).generate_schedule(
                ParentOrder{1, "AAPL", Side::Buy, quantity, minute(0), minute(40)}, {});
        CHECK(total(schedule) == quantity);
        for (const ChildOrder& child : schedule) {
            CHECK(child.quantity > 0);
        }
    }
}

void negative_and_zero_total_weights_are_rejected() {
    EXPECT_THROW(profile({0.5, -0.1, 0.6}), std::invalid_argument);
    EXPECT_THROW(profile({0.0, 0.0, 0.0}), std::invalid_argument);
}

void invalid_and_overlapping_intervals_are_rejected() {
    EXPECT_THROW(
        VolumeProfile({VolumeBucket{minute(0), minute(0), 1.0}}),
        std::invalid_argument);
    EXPECT_THROW(
        VolumeProfile({VolumeBucket{minute(0), minute(10), 0.5},
                       VolumeBucket{minute(5), minute(15), 0.5}}),
        std::invalid_argument);
}

void profile_outside_parent_horizon_is_rejected() {
    const VWAP algorithm{{VolumeProfile({VolumeBucket{minute(0), minute(50), 1.0}})}};
    EXPECT_THROW(algorithm.generate_schedule(parent(100), {}), std::invalid_argument);
}

void non_uniform_vwap_differs_from_twap() {
    const auto vwap =
        VWAP({profile({0.10, 0.20, 0.30, 0.40})}).generate_schedule(parent(1'000), {});
    const auto twap = TWAP({4}).generate_schedule(parent(1'000), {});
    CHECK(vwap[0].quantity == 100);
    CHECK(twap[0].quantity == 250);
    CHECK(vwap != twap);
}

void repeated_allocation_is_deterministic() {
    const VWAP algorithm{{profile({0.13, 0.27, 0.60})}};
    const ParentOrder order = parent(997);
    const ExecutionContext context{100, 200};
    CHECK(algorithm.generate_schedule(order, context) ==
          algorithm.generate_schedule(order, context));
}

}  // namespace

int main() {
    simple_uniform_profile_allocates_evenly();
    non_uniform_profile_matches_expected_weights();
    profile_weights_are_normalized();
    largest_remainder_rounding_is_deterministic();
    quantity_invariant_holds_after_rounding();
    negative_and_zero_total_weights_are_rejected();
    invalid_and_overlapping_intervals_are_rejected();
    profile_outside_parent_horizon_is_rejected();
    non_uniform_vwap_differs_from_twap();
    repeated_allocation_is_deterministic();
    return test_support::failures == 0 ? 0 : 1;
}
