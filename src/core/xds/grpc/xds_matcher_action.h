
#include "src/core/xds/grpc/xds_matcher.h"

namespace grpc_core {

    // Dummy BUcket action
    // Need to add RLQS bucket config action 
class BucketingAction : public XdsMatcher::Action {
 public:
  struct BucketConfig {
    absl::flat_hash_map<std::string, std::string> map;
  };

  explicit BucketingAction(BucketConfig config) : bucket_config_(std::move(config)) {}

  absl::string_view type_url() const override { return "sampleAction"; }
  absl::string_view GetConfigValue(absl::string_view key) {
    return bucket_config_.map[key];
  }

 private:
  BucketConfig bucket_config_;
};

}  // namespace grpc_core