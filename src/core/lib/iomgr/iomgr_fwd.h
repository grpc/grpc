// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_IOMGR_FWD_H
#define GRPC_CORE_LIB_IOMGR_IOMGR_FWD_H

// A bunch of forward declarations that are useful to higher level things that
// don't want to depend on all of iomgr.

#include <grpc/support/port_platform.h>

typedef struct grpc_pollset_set grpc_pollset_set;
typedef struct grpc_pollset grpc_pollset;

#endif  // GRPC_CORE_LIB_IOMGR_IOMGR_FWD_H
