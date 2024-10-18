#ifndef THIRD_PARTY_GRPC_SRC_CORE_LIB_SECURITY_CONTEXT_CONNECTION_AUTH_CONTEXT_H_
#define THIRD_PARTY_GRPC_SRC_CORE_LIB_SECURITY_CONTEXT_CONNECTION_AUTH_CONTEXT_H_

#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <atomic>
#include <iosfwd>
#include <memory>
#include <utility>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/util/alloc.h"

namespace grpc_core {

class ConnectionAuthContext;

namespace auth_context_detail {

template <typename T>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void DestroyAuthProperty(void* p) {
  T::Destroy(static_cast<typename T::ValueType*>(p));
}

// Tracks all registered/known auth property types (these should only be
// registered via AuthPropertyTraits at static initialization time).
class BaseAuthPropertiesTraits {
 public:
  // Count of number of registered auth context properties.
  static uint16_t NumAuthProperties() {
    return static_cast<uint16_t>(RegisteredTraits().size());
  }

  // Number of bytes required to store pointers of all registered auth context
  // properties.
  static size_t Size() { return NumAuthProperties() * sizeof(void*); }

  // Call the registered destruction function for a context.
  static void Destroy(uint16_t id, void* ptr) {
    if (ptr == nullptr) return;
    RegisteredTraits()[id](ptr);
  }

  // Allocate a new context id and register the destruction function.
  template <typename T>
  static uint16_t AllocateId() {
    auto& traits = RegisteredTraits();
    const uint16_t id = static_cast<uint16_t>(traits.size());
    traits.push_back(DestroyAuthProperty<T>);
    return id;
  }

 private:
  // Allocate a new context id and register the destruction function.

  static std::vector<void (*)(void*)>& RegisteredTraits() {
    static std::vector<void (*)(void*)> registered_traits;
    return registered_traits;
  }
};

}  // namespace auth_context_detail

class ConnectionAuthContext final : public Orphanable {
 public:
  static OrphanablePtr<ConnectionAuthContext> Create();

  // Sets the value of a registered property if it is not already set. Return
  // false if the property is already set. If the property is not already
  // set, an object of type Which::ValueType is created using the passed args
  // and stored in the map.
  template <typename Which, typename... Args>
  bool SetIfUnset(Which, Args&&... args) {
    typename Which::ValueType* value =
        static_cast<Which::ValueType*>(registered_properties()[Which::id()]);
    if (value == nullptr) {
      registered_properties()[Which::id()] = Which::Construct(args...);
      return true;
    }
    return false;
  }

  // Force updates the value of a registered property. It deletes any previously
  // set value.
  template <typename Which, typename... Args>
  void Update(Which, Args&&... args) {
    typename Which::ValueType* prev =
        static_cast<Which::ValueType*>(registered_properties()[Which::id()]);
    if (prev != nullptr) {
      Which::Destroy(prev);
    }
    registered_properties()[Which::id()] = Which::Construct(args...);
  }

  // Returns the value of a registered property. If the property is not set,
  // returns nullptr.
  template <typename Which>
  const Which::ValueType* Get() {
    return static_cast<const Which::ValueType*>(
        registered_properties()[Which::id()]);
  }

  void Orphan() override;

 private:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void** registered_properties() {
    return reinterpret_cast<void**>(this + 1);
  }

  ConnectionAuthContext();

  ~ConnectionAuthContext();
};

}  // namespace grpc_core

#endif  // THIRD_PARTY_GRPC_SRC_CORE_LIB_SECURITY_CONTEXT_CONNECTION_AUTH_CONTEXT_H_
