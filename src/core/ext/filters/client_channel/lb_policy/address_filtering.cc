//
// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy/address_filtering.h"

#include "src/core/lib/channel/channel_args.h"

#define GRPC_ARG_HIERARCHICAL_PATH "grpc.internal.address.hierarchical_path"

namespace grpc_core {

namespace {

void* HierarchicalPathCopy(void* p) {
  std::vector<std::string>* path = static_cast<std::vector<std::string>*>(p);
  return static_cast<void*>(new std::vector<std::string>(*path));
}

void HierarchicalPathDestroy(void* p) {
  std::vector<std::string>* path = static_cast<std::vector<std::string>*>(p);
  delete path;
}

int HierarchicalPathCompare(void* p1, void* p2) {
  std::vector<std::string>* path1 = static_cast<std::vector<std::string>*>(p1);
  std::vector<std::string>* path2 = static_cast<std::vector<std::string>*>(p2);
  for (size_t i = 0; i < path1->size(); ++i) {
    if (path2->size() == i) return 1;
    int r = (*path1)[i].compare((*path2)[i]);
    if (r != 0) return r;
  }
  if (path2->size() > path1->size()) return -1;
  return 0;
}

const grpc_arg_pointer_vtable hierarchical_path_arg_vtable = {
    HierarchicalPathCopy, HierarchicalPathDestroy, HierarchicalPathCompare};

}  // namespace

grpc_arg MakeHierarchicalPathArg(const std::vector<std::string>& path) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_HIERARCHICAL_PATH),
      const_cast<std::vector<std::string>*>(&path),
      &hierarchical_path_arg_vtable);
}

HierarchicalAddressMap MakeHierarchicalAddressMap(
    const ServerAddressList& addresses) {
  HierarchicalAddressMap result;
  for (const ServerAddress& address : addresses) {
    auto* path = grpc_channel_args_find_pointer<std::vector<std::string>>(
        address.args(), GRPC_ARG_HIERARCHICAL_PATH);
    if (path == nullptr || path->empty()) continue;
    auto it = path->begin();
    ServerAddressList& target_list = result[*it];
    ++it;
    std::vector<std::string> remaining_path(it, path->end());
    const char* name_to_remove = GRPC_ARG_HIERARCHICAL_PATH;
    grpc_arg new_arg = MakeHierarchicalPathArg(remaining_path);
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
        address.args(), &name_to_remove, 1, &new_arg, 1);
    target_list.emplace_back(address.address(), new_args);
  }
  return result;
}

}  // namespace grpc_core
