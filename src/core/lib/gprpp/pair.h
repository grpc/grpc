#ifndef GRPC_CORE_LIB_GPRPP_PAIR_H_
#define GRPC_CORE_LIB_GPRPP_PAIR_H_
namespace grpc_core {
// Alternative to std::pair for grpc_core
template <class Key, class T>
struct pair {
 public:
  pair(Key k, T v) : first(std::move(k)), second(std::move(v)) {}
  void swap(pair& other) {
    Key temp_first = std::move(first);
    T temp_second = std::move(second);
    first = std::move(other.first);
    second = std::move(other.second);
    other.first = std::move(temp_first);
    other.second = std::move(temp_second);
  }
  Key first;
  T second;

 private:
};

template <class Key, class T>
pair<Key, T> make_pair(Key&& k, T&& v) {
  return std::move(pair<Key, T>(std::move(k), std::move(v)));
}
}  // namespace grpc_core
#endif  // GRPC_CORE_LIB_GPRPP_PAIR_H_