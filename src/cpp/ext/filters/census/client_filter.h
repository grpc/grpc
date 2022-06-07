/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CLIENT_FILTER_H
#define GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CLIENT_FILTER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/error.h"
#include "src/cpp/common/channel_filter.h"
#include "src/cpp/ext/filters/census/open_census_call_tracer.h"

namespace grpc {

// A CallData class will be created for every grpc call within a channel. It is
// used to store data and methods specific to that call. CensusClientCallData is
// thread-compatible, however typically only 1 thread should be interacting with
// a call at a time.
class CensusClientCallData : public CallData {
 public:
  grpc_error_handle Init(grpc_call_element* /* elem */,
                         const grpc_call_element_args* args) override;
  void StartTransportStreamOpBatch(grpc_call_element* elem,
                                   TransportStreamOpBatch* op) override;

 private:
  OpenCensusCallTracer* tracer_ = nullptr;
};

}  // namespace grpc

#endif /* GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CLIENT_FILTER_H */
