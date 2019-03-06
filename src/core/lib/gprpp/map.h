#ifndef GRPC_CORE_LIB_GPRPP_MAP_H_
#define GRPC_CORE_LIB_GPRPP_MAP_H_

#include <map>
namespace grpc_core {
template <class Key, class T, class Compare = std::less<Key>>
using Map =
    std::map<Key, T, Compare, grpc_core::Allocator<std::pair<const Key, T>>>;
}  // namespace grpc_core
#endif  // GRPC_CORE_LIB_GPRPP_MAP_H_
