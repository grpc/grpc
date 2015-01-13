#include <limits>

#include "grpc/grpc.h"
#include "grpc/support/time.h"
#include "timeval.h"

namespace grpc {
namespace node {

gpr_timespec MillisecondsToTimespec(double millis) {
  if (millis == std::numeric_limits<double>::infinity()) {
    return gpr_inf_future;
  } else if (millis == -std::numeric_limits<double>::infinity()) {
    return gpr_inf_past;
  } else {
    return gpr_time_from_micros(static_cast<int64_t>(millis*1000));
  }
}

double TimespecToMilliseconds(gpr_timespec timespec) {
  if (gpr_time_cmp(timespec, gpr_inf_future) == 0) {
    return std::numeric_limits<double>::infinity();
  } else if (gpr_time_cmp(timespec, gpr_inf_past) == 0) {
    return -std::numeric_limits<double>::infinity();
  } else {
    struct timeval time = gpr_timeval_from_timespec(timespec);
    return (static_cast<double>(time.tv_sec) * 1000 +
            static_cast<double>(time.tv_usec) / 1000);
  }
}

}  // namespace node
}  // namespace grpc
