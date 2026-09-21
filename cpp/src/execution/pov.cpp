#include "quant_engine/execution/pov.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quant_engine::execution {

POV::POV(const POVConfig config) : config_{config} {
    if (!std::isfinite(config.participation_rate) ||
        config.participation_rate <= 0.0 || config.participation_rate > 1.0) {
        throw std::invalid_argument("POV participation rate must be in (0, 1]");
    }
    if (config.child_order_type != OrderType::Market) {
        throw std::invalid_argument(
            "POV limit children require a price policy; Set 9 supports market children");
    }
}

std::optional<ChildOrder> POV::generate_child(
    const ParentOrder& parent, const MarketVolumeObservation& observation,
    const OrderId child_order_id,
    const SequenceNumber schedule_sequence) const {
    if (!parent.can_generate_children() || observation.volume == 0) {
        return std::nullopt;
    }
    if (child_order_id == 0 || schedule_sequence == 0) {
        throw std::invalid_argument("POV child id and sequence must be positive");
    }
    const long double target =
        static_cast<long double>(config_.participation_rate) *
        static_cast<long double>(observation.volume);
    const Quantity desired = static_cast<Quantity>(std::floor(target));
    const Quantity quantity = std::min(desired, parent.remaining_quantity());
    if (quantity == 0) {
        return std::nullopt;
    }
    return ChildOrder{
        .order_id = child_order_id,
        .parent_order_id = parent.parent_order_id(),
        .side = parent.side(),
        .quantity = quantity,
        .type = config_.child_order_type,
        .price = std::nullopt,
        .scheduled_time = observation.timestamp,
        .schedule_sequence = schedule_sequence,
    };
}

}  // namespace quant_engine::execution
