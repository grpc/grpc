
#include "src/core/xds/grpc/xds_matcher_context.h"


namespace grpc_core {

std::optional<absl::string_view> RpcMatchContext::GetHeaderValue(
    absl::string_view header_name, std::string* concatenated_value) const {
  if (absl::EndsWith(header_name, "-bin")) {
    return std::nullopt;
  } else if (header_name == "content-type") {
    return "application/grpc";
  }
  return initial_metadata->GetStringValue(header_name, concatenated_value);
}

}  // namespace grpc_core
