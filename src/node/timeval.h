#ifndef NET_GRPC_NODE_TIMEVAL_H_
#define NET_GRPC_NODE_TIMEVAL_H_

#include "grpc/support/time.h"

namespace grpc {
namespace node {

double TimespecToMilliseconds(gpr_timespec time);
gpr_timespec MillisecondsToTimespec(double millis);

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_TIMEVAL_H_
