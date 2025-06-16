
#include "src/core/call/metadata_batch.h"
#include "src/core/xds/grpc/xds_matcher.h"

namespace grpc_core {

class RpcMatchContext : public XdsMatcher::MatchContext {
 public:
  explicit RpcMatchContext(grpc_metadata_batch* initial_metadata)
      : initial_metadata(initial_metadata) {}

  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("rpc_context");
  }

  UniqueTypeName type() const override { return Type(); }

  // Returns the metadata value(s) for the specified key.
  // As special cases, binary headers return a value of std::nullopt, and
  // "content-type" header returns "application/grpc".
  std::optional<absl::string_view> GetHeaderValue(
      absl::string_view header_name, std::string* concatenated_value) const;

  // FIXME: add other methods here
 private:
  grpc_metadata_batch* initial_metadata;
};
}  // namespace grpc_core