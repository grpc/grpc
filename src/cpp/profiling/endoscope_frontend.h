#include "src/core/profiling/endoscope_backend.h"
#include "src/cpp/profiling/endoscope.pb.h"

#ifndef ENDOSCOPE_FRONTEND_H_
#define ENDOSCOPE_FRONTEND_H_

namespace perftools {
namespace endoscope {

void WriteSnapshot(EndoBase *base, EndoSnapshotPB* snapshot,
                   ENDO_INT64 url_cycle_begin, ENDO_INT64 url_cycle_end);

}  // namespace endoscope
}  // namespace perftools

#endif  /* ENDOSCOPE_FRONTEND_H_ */
