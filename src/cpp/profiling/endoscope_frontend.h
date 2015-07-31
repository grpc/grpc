#include "src/core/profiling/endoscope_backend.h"
#include "src/cpp/profiling/endoscope.pb.h"

#ifdef GRPC_ENDOSCOPE_PROFILER

#ifndef GRPC_INTERNAL_CPP_PROFILING_ENDOSCOPE_FRONTEND_H
#define GRPC_INTERNAL_CPP_PROFILING_ENDOSCOPE_FRONTEND_H

namespace perftools {
namespace endoscope {

void WriteSnapshot(grpc_endo_base *base, EndoSnapshotPB* snapshot,
                   gpr_int64 url_cycle_begin, gpr_int64 url_cycle_end);

}  // namespace endoscope
}  // namespace perftools

#endif  // GRPC_INTERNAL_CPP_PROFILING_ENDOSCOPE_FRONTEND_H

#endif  // GRPC_ENDOSCOPE_PROFILER
