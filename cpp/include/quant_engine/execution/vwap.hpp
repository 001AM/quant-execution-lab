#pragma once

#include "quant_engine/execution/execution_algo.hpp"
#include "quant_engine/execution/volume_profile.hpp"

namespace quant_engine::execution {

struct VWAPConfig {
    VolumeProfile volume_profile;
    OrderType child_order_type{OrderType::Market};
};

class VWAP final : public ExecutionAlgorithm {
public:
    explicit VWAP(VWAPConfig config);

    [[nodiscard]] std::vector<ChildOrder> generate_schedule(
        const ParentOrder& parent,
        const ExecutionContext& context) const override;

private:
    VWAPConfig config_;
};

}  // namespace quant_engine::execution
