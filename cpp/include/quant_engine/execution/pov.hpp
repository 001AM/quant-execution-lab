#pragma once

#include "quant_engine/execution/parent_order.hpp"

#include <optional>

namespace quant_engine::execution {

struct POVConfig {
    double participation_rate;
    OrderType child_order_type{OrderType::Market};
};

class POV {
public:
    explicit POV(POVConfig config);

    [[nodiscard]] std::optional<ChildOrder> generate_child(
        const ParentOrder& parent, const MarketVolumeObservation& observation,
        OrderId child_order_id, SequenceNumber schedule_sequence) const;

    [[nodiscard]] double participation_rate() const noexcept {
        return config_.participation_rate;
    }

private:
    POVConfig config_;
};

}  // namespace quant_engine::execution
