//
//
// Copyright 2018 gRPC authors.
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
//
//

#include "src/core/lib/iomgr/pollset_set.h"

#include <grpc/support/port_platform.h>

grpc_pollset_set_vtable* grpc_pollset_set_impl;

void grpc_set_pollset_set_vtable(grpc_pollset_set_vtable* vtable) {
  grpc_pollset_set_impl = vtable;
}

grpc_pollset_set* grpc_pollset_set_create() {
  return grpc_pollset_set_impl->create();
}

void grpc_pollset_set_destroy(grpc_pollset_set* pollset_set) {
  grpc_pollset_set_impl->destroy(pollset_set);
}

void grpc_pollset_set_add_pollset(grpc_pollset_set* pollset_set,
                                  grpc_pollset* pollset) {
  grpc_pollset_set_impl->add_pollset(pollset_set, pollset);
}

void grpc_pollset_set_del_pollset(grpc_pollset_set* pollset_set,
                                  grpc_pollset* pollset) {
  grpc_pollset_set_impl->del_pollset(pollset_set, pollset);
}

void grpc_pollset_set_add_pollset_set(grpc_pollset_set* bag,
                                      grpc_pollset_set* item) {
  grpc_pollset_set_impl->add_pollset_set(bag, item);
}

void grpc_pollset_set_del_pollset_set(grpc_pollset_set* bag,
                                      grpc_pollset_set* item) {
  grpc_pollset_set_impl->del_pollset_set(bag, item);
}
