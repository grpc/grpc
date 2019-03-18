#ifndef GRPC_CORE_LIB_GPRPP_MAP_TESTER_H_
#define GRPC_CORE_LIB_GPRPP_MAP_TESTER_H_

#include "src/core/lib/gprpp/map.h"

namespace grpc_core {
template <class Key, class T, class Compare>
class MapTester {
 public:
  MapTester(grpc_core::map<Key, T, Compare>* test_map) : map_(test_map) {}
  typename grpc_core::map<Key, T, Compare>::Entry* Root() {
    return map_->root();
  }

  typename grpc_core::map<Key, T, Compare>::Entry* Left(
      typename grpc_core::map<Key, T, Compare>::Entry* e) {
    return e->left();
  }

  typename grpc_core::map<Key, T, Compare>::Entry* Right(
      typename grpc_core::map<Key, T, Compare>::Entry* e) {
    return e->right();
  }

 private:
  grpc_core::map<Key, T, Compare>* map_;
};
}  // namespace grpc_core
#endif  // GRPC_CORE_LIB_GPRPP_MAP_TESTER_H_
