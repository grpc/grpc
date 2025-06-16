#include "src/core/xds/grpc/xds_matcher.h"

namespace grpc_core {

class MetadataInput : public XdsMatcher::InputValue<absl::string_view> {
  public:
    explicit MetadataInput(absl::string_view key) : key_(key) {}

    // The supported MatchContext type.
    // When validating an xDS resource, if an input is specified in a
    // context that it doesn't support, the resource should be NACKed.
    UniqueTypeName context_type() const override {
        return GRPC_UNIQUE_TYPE_NAME_HERE("rpc_context");
    };

    // Gets the value to be matched from context.
    std::optional<absl::string_view> GetValue(const XdsMatcher::MatchContext& context) const override;
  private:
    std::string key_;
};

}  // namespace grpc_core
