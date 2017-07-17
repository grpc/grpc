/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef NET_GRPC_HHVM_GRPC_CALL_H_
#define NET_GRPC_HHVM_GRPC_CALL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"
#include "channel.h"
#include "timeval.h"

#include "grpc/grpc.h"

namespace HPHP {

class CallData {
  private:
    grpc_call* wrapped{nullptr};
    bool owned = false;
    ChannelData* channelData{nullptr};
    int32_t timeout;
  public:
    static Class* s_class;
    static const StaticString s_className;

    static Class* getClass();

    CallData();
    ~CallData();

    void init(grpc_call* call);
    void sweep();
    grpc_call* getWrapped();
    bool getOwned();
    void setChannelData(ChannelData* channelData_);
    void setOwned(bool owned_);
    void setTimeout(int32_t timeout_);
    int32_t getTimeout();
};

void *cq_pluck_async(void *params_ptr);

typedef struct cq_pluck_async_params {
  grpc_completion_queue* cq;
  void* tag;
  gpr_timespec deadline;
  void* reserved;
  int fd;
} cq_pluck_async_params;

void HHVM_METHOD(Call, __construct,
  const Object& channel_obj,
  const String& method,
  const Object& deadline_obj,
  const Variant& host_override /* = null */);

Object HHVM_METHOD(Call, startBatch,
  const Array& actions);

String HHVM_METHOD(Call, getPeer);

void HHVM_METHOD(Call, cancel);

int64_t HHVM_METHOD(Call, setCredentials,
  const Object& creds_obj);

Variant grpc_parse_metadata_array(grpc_metadata_array *metadata_array);
bool hhvm_create_metadata_array(const Array& array, grpc_metadata_array *metadata);

};

#endif /* NET_GRPC_HHVM_GRPC_CALL_H_ */
