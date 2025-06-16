#include "src/core/xds/grpc/xds_matcher_input.h"
#include "src/core/xds/grpc/xds_matcher_context.h"



namespace grpc_core {
std::optional<absl::string_view> MetadataInput::GetValue(const XdsMatcher::MatchContext& context) const {
    assert(context.type() == RpcMatchContext::Type());
    std::string buffer;
    return DownCast<const RpcMatchContext&>(context).GetHeaderValue(key_, &buffer);
}
}