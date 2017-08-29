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

#ifndef GRPCXX_IMPL_CODEGEN_METADATA_MAP_H
#define GRPCXX_IMPL_CODEGEN_METADATA_MAP_H

#include <grpc++/impl/codegen/slice.h>

namespace grpc {

class MetadataMap {
 public:
  MetadataMap() { memset(&arr_, 0, sizeof(arr_)); }

  ~MetadataMap() {
    g_core_codegen_interface->grpc_metadata_array_destroy(&arr_);
  }

  void FillMap() {
    for (size_t i = 0; i < arr_.count; i++) {
      // TODO(yangg) handle duplicates?
      map_.insert(std::pair<grpc::string_ref, grpc::string_ref>(
          StringRefFromSlice(&arr_.metadata[i].key),
          StringRefFromSlice(&arr_.metadata[i].value)));
    }
  }

  std::multimap<grpc::string_ref, grpc::string_ref> *map() { return &map_; }
  const std::multimap<grpc::string_ref, grpc::string_ref> *map() const {
    return &map_;
  }
  grpc_metadata_array *arr() { return &arr_; }

 private:
  grpc_metadata_array arr_;
  std::multimap<grpc::string_ref, grpc::string_ref> map_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_METADATA_MAP_H
