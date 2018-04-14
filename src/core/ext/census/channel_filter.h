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

#ifndef GRPC_CORE_EXT_CENSUS_CHANNEL_FILTER_H
#define GRPC_CORE_EXT_CENSUS_CHANNEL_FILTER_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/census/filter.h"

namespace opencensus {

// A ChannelData class will be created for every grpc channel. It is used to
// store channel wide data and methods. CensusChannelData is thread-compatible.
// Channel filters specify:
//    1. the amount of memory needed in the channel & call (via the sizeof_XXX
//       members)
//    2. functions to initialize and destroy channel & call data
//       (init_XXX, destroy_XXX)
//    3. functions to implement call operations and channel operations (call_op,
//       channel_op)
//    4. a name, which is useful when debugging
// (See grpc/src/core/lib/channel/channel_stack.h for more information.)
class CensusChannelData : public grpc::ChannelData {
 public:
  grpc_error* Init(grpc_channel_element* elem,
                   grpc_channel_element_args* args) override;
};

}  // namespace opencensus

#endif /* GRPC_CORE_EXT_CENSUS_CHANNEL_FILTER_H */
