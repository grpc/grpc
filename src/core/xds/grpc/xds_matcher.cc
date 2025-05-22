
#include "src/core/xds/grpc/xds_matcher.h"
#include "xds/type/matcher/v3/matcher.upb.h"

namespace matcher {

    int parseFunc(const xds_type_matcher_v3_Matcher *match) {
    switch (xds_type_matcher_v3_Matcher_matcher_type_case(match)) {
        case xds_type_matcher_v3_Matcher_matcher_type_matcher_list:
            return 1;
        case xds_type_matcher_v3_Matcher_matcher_type_matcher_tree:
            return 2;
        default:
            return 0;
    }
    return -1;
}

}  // namespace matcher