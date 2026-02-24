#ifndef GRPCPP_IMPL_CALL_CONTEXT_REGISTRY_H
#define GRPCPP_IMPL_CALL_CONTEXT_REGISTRY_H

#include <cstdint>

namespace grpc_core {
class Arena;
}  // namespace grpc_core

namespace grpc {
namespace impl {

class CallContextRegistry {
 public:
  // An opaque type to be used in ClientContext.
  using ElementList = void**;

  // Adds an element to elements.
  template <typename T>
  static void SetContext(T element, ElementList& elements) {
    uint16_t id = grpc::impl::CallContextType<T>::id();
    if (elements == nullptr) {
      uint16_t num_elements = Count();
      elements = new void*[num_elements]();
    }
    DestroyElement(id, elements[id]);
    elements[id] = new T(std::move(element));
  }

  // Called when starting the C-core call.
  // Passes ownership of all elements into the C-core arena.
  // Deletes elements and resets it to nullptr.
  static void Propagate(ElementList& elements, grpc_core::Arena* arena);

  // Called on ClientContext destruction.  No-op if elements is already null.
  // Otherwise, deletes the context elements and deletes elements.
  static void Destroy(ElementList& elements);

 private:
  static uint16_t Count();

  static void DestroyElement(uint16_t id, void* element);

  static uint16_t Register(void (*destroy)(void*),
                           void (*propagate)(void*, grpc_core::Arena*));

  template <typename T>
  friend struct CallContextTypeBase;
};

// Must define Propagate
template <typename T>
struct CallContextType;

template <typename T>
struct CallContextTypeBase {
 public:
  static uint16_t id() {
    static uint16_t id = CallContextRegistry::Register(
        &CallContextTypeBase<T>::Destroy, &CallContextTypeBase<T>::Propagate);
    return id;
  }

 private:
  static void Destroy(void* p) { delete static_cast<T*>(p); }

  static void Propagate(void* p, grpc_core::Arena* a) {
    CallContextType<T>::Propagate(static_cast<T*>(p), a);
  }
};

}  // namespace impl
}  // namespace grpc

#endif  // GRPCPP_IMPL_CALL_CONTEXT_REGISTRY_H
