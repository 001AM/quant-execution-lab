#include "quant_engine/execution/vwap.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <stdexcept>
#include <utility>

namespace quant_engine::execution {
namespace {

struct RemainderRank {
    std::size_t index;
    long double fractional;
};

}  // namespace

VWAP::VWAP(VWAPConfig config) : config_{std::move(config)} {
    if (config_.child_order_type != OrderType::Market) {
        throw std::invalid_argument(
            "VWAP limit children require a price policy; Set 9 supports market children");
    }
}

std::vector<ChildOrder> VWAP::generate_schedule(
    const ParentOrder& parent, const ExecutionContext& context) const {
    if (!parent.can_generate_children()) {
        return {};
    }
    if (context.first_child_order_id == 0 ||
        context.first_schedule_sequence == 0) {
        throw std::invalid_argument("VWAP child ids and sequences must be positive");
    }
    const auto& buckets = config_.volume_profile.buckets();
    if (buckets.front().start < parent.start_time() ||
        buckets.back().end > parent.end_time()) {
        throw std::invalid_argument("volume profile must lie within parent execution horizon");
    }

    std::vector<Quantity> allocations(buckets.size(), 0);
    std::vector<RemainderRank> ranks;
    ranks.reserve(buckets.size());
    Quantity allocated = 0;
    for (std::size_t index = 0; index < buckets.size(); ++index) {
        const long double target =
            static_cast<long double>(parent.remaining_quantity()) *
            static_cast<long double>(buckets[index].weight);
        const auto floor_quantity = static_cast<Quantity>(std::floor(target));
        allocations[index] = floor_quantity;
        allocated += floor_quantity;
        ranks.push_back(RemainderRank{index, target - std::floor(target)});
    }
    std::stable_sort(
        ranks.begin(), ranks.end(),
        [](const RemainderRank& left, const RemainderRank& right) {
            return left.fractional > right.fractional;
        });
    Quantity units_to_allocate = parent.remaining_quantity() - allocated;
    for (std::size_t rank = 0; units_to_allocate > 0; ++rank) {
        ++allocations[ranks[rank % ranks.size()].index];
        --units_to_allocate;
    }

    std::vector<ChildOrder> result;
    result.reserve(buckets.size());
    if (buckets.size() - 1 >
            std::numeric_limits<OrderId>::max() - context.first_child_order_id ||
        buckets.size() - 1 > std::numeric_limits<SequenceNumber>::max() -
                                 context.first_schedule_sequence) {
        throw std::overflow_error("VWAP child id or sequence overflow");
    }
    OrderId next_id = context.first_child_order_id;
    SequenceNumber next_sequence = context.first_schedule_sequence;
    for (std::size_t index = 0; index < buckets.size(); ++index) {
        if (allocations[index] == 0) {
            continue;
        }
        result.push_back(ChildOrder{
            .order_id = next_id++,
            .parent_order_id = parent.parent_order_id(),
            .side = parent.side(),
            .quantity = allocations[index],
            .type = config_.child_order_type,
            .price = std::nullopt,
            .scheduled_time = buckets[index].start,
            .schedule_sequence = next_sequence++,
        });
    }
    return result;
}

}  // namespace quant_engine::execution
