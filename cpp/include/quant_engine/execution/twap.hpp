#pragma once

#include "quant_engine/execution/execution_algo.hpp"

#include <cstddef>

namespace quant_engine::execution {

struct TWAPConfig {
    std::size_t slices;
    OrderType child_order_type{OrderType::Market};
};

class TWAP final : public ExecutionAlgorithm {
public:
    explicit TWAP(TWAPConfig config);

    [[nodiscard]] std::vector<ChildOrder> generate_schedule(
        const ParentOrder& parent,
        const ExecutionContext& context) const override;

private:
    TWAPConfig config_;
};

}  // namespace quant_engine::execution
