// Copyright 2024 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_TRANSPORT_H

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"

namespace grpc_core {
namespace chaotic_good {

class FrameTransport {
 public:
  virtual void StartReading(Party* party, MpscSender<Frame> frames) = 0;
  virtual void StartWriting(Party* party, MpscReceiver<Frame> frames) = 0;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif
