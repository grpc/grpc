/*
 *
 * Copyright 2022 gRPC authors.
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

#ifdef GRPC_DTRACE
#include "dtrace_provider.h"
#else
#define GRPC_TRANSPORT_SEND_INITIAL_METADATA_ENABLED() (0)
#define GRPC_TRANSPORT_SEND_INITIAL_METADATA(...)
#define GRPC_TRANSPORT_SEND_MESSAGE_ENABLED() (0)
#define GRPC_TRANSPORT_SEND_MESSAGE(...)
#define GRPC_TRANSPORT_SEND_TRAILING_METADATA_ENABLED() (0)
#define GRPC_TRANSPORT_SEND_TRAILING_METADATA(...)
#define GRPC_TRANSPORT_RECV_INITIAL_METADATA_ENABLED() (0)
#define GRPC_TRANSPORT_RECV_INITIAL_METADATA(...)
#define GRPC_TRANSPORT_RECV_MESSAGE_ENABLED() (0)
#define GRPC_TRANSPORT_RECV_MESSAGE(...)
#define GRPC_TRANSPORT_RECV_TRAILING_METADATA_ENABLED() (0)
#define GRPC_TRANSPORT_RECV_TRAILING_METADATA(...)
#define GRPC_TRANSPORT_CANCEL_STREAM_ENABLED() (0)
#define GRPC_TRANSPORT_CANCEL_STREAM(...)
#endif
