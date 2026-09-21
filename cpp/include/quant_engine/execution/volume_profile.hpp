#pragma once

#include "quant_engine/types.hpp"

#include <vector>

namespace quant_engine::execution {

struct VolumeBucket {
    Timestamp start;
    Timestamp end;
    double weight;
};

class VolumeProfile {
public:
    explicit VolumeProfile(std::vector<VolumeBucket> buckets);

    [[nodiscard]] const std::vector<VolumeBucket>& buckets() const noexcept {
        return buckets_;
    }

private:
    std::vector<VolumeBucket> buckets_;
};

}  // namespace quant_engine::execution
