#include "quant_engine/execution/volume_profile.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace quant_engine::execution {

VolumeProfile::VolumeProfile(std::vector<VolumeBucket> buckets)
    : buckets_{std::move(buckets)} {
    if (buckets_.empty()) {
        throw std::invalid_argument("volume profile must not be empty");
    }
    double total_weight = 0.0;
    for (std::size_t index = 0; index < buckets_.size(); ++index) {
        VolumeBucket& bucket = buckets_[index];
        if (bucket.start >= bucket.end) {
            throw std::invalid_argument("volume bucket start must precede end");
        }
        if (!std::isfinite(bucket.weight) || bucket.weight < 0.0) {
            throw std::invalid_argument("volume bucket weight must be finite and non-negative");
        }
        if (index > 0 && bucket.start < buckets_[index - 1].end) {
            throw std::invalid_argument(
                "volume buckets must be chronological and non-overlapping");
        }
        total_weight += bucket.weight;
    }
    if (!std::isfinite(total_weight) || total_weight <= 0.0) {
        throw std::invalid_argument("volume profile total weight must be positive");
    }
    for (VolumeBucket& bucket : buckets_) {
        bucket.weight /= total_weight;
    }
}

}  // namespace quant_engine::execution
