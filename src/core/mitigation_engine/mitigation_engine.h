#ifndef THIRD_PARTY_GRPC_SRC_CORE_MITIGATION_ENGINE_MITIGATION_ENGINE_H_
#define THIRD_PARTY_GRPC_SRC_CORE_MITIGATION_ENGINE_MITIGATION_ENGINE_H_

#include <optional>

#include "src/core/call/metadata_batch.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/useful.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class MitigationEngine : public grpc_core::RefCounted<MitigationEngine> {
 public:
  enum class Action { kCloseConnection, kRejectRpc };
  static absl::string_view ActionToString(Action action) {
    switch (action) {
      case Action::kCloseConnection:
        return "Close Connection";
      case Action::kRejectRpc:
        return "Reject RPC";
    }
    return "Unknown Action";
  }
  virtual ~MitigationEngine() = default;
  // Evaluates an incoming connection based on the peer address.
  virtual std::optional<Action> EvaluateIncomingConnection(
      absl::string_view peer_address) = 0;
  // Evaluates an incoming metadata key/value pair. Please note that this
  // function only evaluates *new* key/value pairs; if an incoming header has
  // already been seen by the connection, it will not be evaluated again.
  virtual std::optional<Action> EvaluateIncomingMetadata(
      absl::string_view key, absl::string_view value) = 0;
  // Evaluates all incoming metadata after they have been parsed.
  virtual std::optional<Action> EvaluateAllIncomingMetadata(
      const grpc_metadata_batch& metadata) = 0;
};

class MitigationEngineProvider
    : public DualRefCounted<MitigationEngineProvider> {
 public:
  static absl::string_view ChannelArgName() {
    return "grpc.internal.mitigation_engine_provider";
  }

  static int ChannelArgsCompare(const MitigationEngineProvider* a,
                                const MitigationEngineProvider* b) {
    return QsortCompare(a, b);
  }

  // Returns a reference to the current MitigationEngine, or nullptr if none
  // is configured. An old engine will not be destroyed until all references
  // to it are dropped.
  virtual RefCountedPtr<MitigationEngine> GetEngine() = 0;

  void Orphaned() override {}
};

}  // namespace grpc_core

#endif  // THIRD_PARTY_GRPC_SRC_CORE_MITIGATION_ENGINE_MITIGATION_ENGINE_H_
