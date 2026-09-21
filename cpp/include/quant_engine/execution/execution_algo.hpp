#pragma once

#include "quant_engine/execution/parent_order.hpp"

#include <vector>

namespace quant_engine::execution {

struct ExecutionContext {
    OrderId first_child_order_id{1};
    SequenceNumber first_schedule_sequence{1};
};

class ExecutionAlgorithm {
public:
    virtual ~ExecutionAlgorithm() = default;

    [[nodiscard]] virtual std::vector<ChildOrder> generate_schedule(
        const ParentOrder& parent, const ExecutionContext& context) const = 0;
};

}  // namespace quant_engine::execution
