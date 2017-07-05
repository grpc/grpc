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
#include <atomic>
#include <map>

namespace grpc {

class MetadataMap {
 public:
  void FillMap() {
    GPR_CODEGEN_ASSERT(map_.load(std::memory_order::memory_order_relaxed) ==
                       nullptr);
  }

  std::multimap<grpc::string_ref, grpc::string_ref> *map() {
    return FetchMap();
  }
  const std::multimap<grpc::string_ref, grpc::string_ref> *map() const {
    return FetchMap();
  }
  grpc_metadata **pmetadata() { return &metadata_; }
  size_t *psize() { return &size_; }

 private:
  typedef std::multimap<grpc::string_ref, grpc::string_ref> MapRep;

  MapRep *FetchMap() const {
    MapRep *r = map_.load(std::memory_order::memory_order_acquire);
    if (r == nullptr) {
      r = new MapRep;
      for (size_t i = 0; i < size_; i++) {
        r->insert(std::pair<grpc::string_ref, grpc::string_ref>(
            StringRefFromSlice(&metadata_[i].key),
            StringRefFromSlice(&metadata_[i].value)));
      }
      MapRep *prev = nullptr;
      if (!map_.compare_exchange_strong(
              prev, r, std::memory_order::memory_order_release,
              std::memory_order::memory_order_relaxed)) {
        delete r;
        r = prev;
      }
    }
    return r;
  }

  grpc_metadata *metadata_ = nullptr;
  size_t size_ = 0;
  mutable std::atomic<MapRep *> map_{nullptr};
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_METADATA_MAP_H
