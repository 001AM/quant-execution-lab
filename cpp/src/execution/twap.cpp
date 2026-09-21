#include "quant_engine/execution/twap.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace quant_engine::execution {

TWAP::TWAP(const TWAPConfig config) : config_{config} {
    if (config.slices == 0) {
        throw std::invalid_argument("TWAP slices must be positive");
    }
    if (config.child_order_type != OrderType::Market) {
        throw std::invalid_argument(
            "TWAP limit children require a price policy; Set 9 supports market children");
    }
}

std::vector<ChildOrder> TWAP::generate_schedule(
    const ParentOrder& parent, const ExecutionContext& context) const {
    if (!parent.can_generate_children()) {
        return {};
    }
    if (context.first_child_order_id == 0 ||
        context.first_schedule_sequence == 0) {
        throw std::invalid_argument("TWAP child ids and sequences must be positive");
    }
    const std::size_t slice_count =
        parent.remaining_quantity() < config_.slices
            ? static_cast<std::size_t>(parent.remaining_quantity())
            : config_.slices;
    if (slice_count - 1 >
            std::numeric_limits<OrderId>::max() - context.first_child_order_id ||
        slice_count - 1 > std::numeric_limits<SequenceNumber>::max() -
                                context.first_schedule_sequence) {
        throw std::overflow_error("TWAP child id or sequence overflow");
    }
    const Quantity base = parent.remaining_quantity() / slice_count;
    const Quantity remainder = parent.remaining_quantity() % slice_count;
    const auto interval = (parent.end_time() - parent.start_time()) / slice_count;

    std::vector<ChildOrder> result;
    result.reserve(slice_count);
    for (std::size_t index = 0; index < slice_count; ++index) {
        const Quantity quantity = base + (index < remainder ? 1U : 0U);
        result.push_back(ChildOrder{
            .order_id = context.first_child_order_id + index,
            .parent_order_id = parent.parent_order_id(),
            .side = parent.side(),
            .quantity = quantity,
            .type = config_.child_order_type,
            .price = std::nullopt,
            .scheduled_time = parent.start_time() + interval * index,
            .schedule_sequence = context.first_schedule_sequence + index,
        });
    }
    return result;
}

}  // namespace quant_engine::execution
