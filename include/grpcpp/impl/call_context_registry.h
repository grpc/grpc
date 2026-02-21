#ifndef GRPCPP_IMPL_CALL_CONTEXT_REGISTRY_H
#define GRPCPP_IMPL_CALL_CONTEXT_REGISTRY_H

#include <cstdint>

namespace grpc_core { class Arena; }  // Forward declaration

namespace grpc {
namespace impl {

class CallContextRegistry {
 public:
  static uint16_t Register(void (*destroy)(void*),
                           void (*propagate)(void*, grpc_core::Arena*));

  static void Destroy(uint16_t id, void* ptr);
  
  static void PropagateAll(void** contexts, grpc_core::Arena* arena);
  
  static uint16_t Count();
};

// Must define Propagate
template <typename T>
struct CallContextType;

template <typename T>
struct CallContextTypeBase {
 private:
  static void Destroy(void* p) {
    delete static_cast<T*>(p);
  }

  static void Propagate(void* p, grpc_core::Arena* a) {
    CallContextType<T>::Propagate(static_cast<const T*>(p), a); 
  }

 public:
  static uint16_t id() {
    static uint16_t id_ = CallContextRegistry::Register(
        &CallContextTypeBase<T>::Destroy,
        &CallContextTypeBase<T>::Propagate
    );
    return id_;
  }
};

}  // namespace impl
}  // namespace grpc

#endif  // GRPCPP_IMPL_CALL_CONTEXT_REGISTRY_H