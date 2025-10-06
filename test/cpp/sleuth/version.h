#ifndef THIRD_PARTY_GRPC_TEST_CPP_SLEUTH_VERSION_H_
#define THIRD_PARTY_GRPC_TEST_CPP_SLEUTH_VERSION_H_

#include <cstdint>

namespace grpc_sleuth {

// Version number for the sleuth tool. Update with the current epoch with each
// change and keep it monotonically increasing.
constexpr int64_t kSleuthVersion = 1759775012LL;

}  // namespace grpc_sleuth

#endif  // THIRD_PARTY_GRPC_TEST_CPP_SLEUTH_VERSION_H_
