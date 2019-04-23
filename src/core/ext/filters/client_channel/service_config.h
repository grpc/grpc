//
// Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVICE_CONFIG_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVICE_CONFIG_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/slice/slice_hash_table.h"

// The main purpose of the code here is to parse the service config in
// JSON form, which will look like this:
//
// {
//   "loadBalancingPolicy": "string",  // optional
//   "methodConfig": [  // array of one or more method_config objects
//     {
//       "name": [  // array of one or more name objects
//         {
//           "service": "string",  // required
//           "method": "string",  // optional
//         }
//       ],
//       // remaining fields are optional.
//       // see https://developers.google.com/protocol-buffers/docs/proto3#json
//       // for format details.
//       "waitForReady": bool,
//       "timeout": "duration_string",
//       "maxRequestMessageBytes": "int64_string",
//       "maxResponseMessageBytes": "int64_string",
//     }
//   ]
// }

namespace grpc_core {

/// This is the base class that all service config parsers MUST use to store
/// parsed service config data.
class ServiceConfigParsedObject {
 public:
  virtual ~ServiceConfigParsedObject() = default;

  GRPC_ABSTRACT_BASE_CLASS;
};

/// This is the base class that all service config parsers should derive from.
class ServiceConfigParser {
 public:
  virtual ~ServiceConfigParser() = default;

  virtual UniquePtr<ServiceConfigParsedObject> ParseGlobalParams(
      const grpc_json* json, grpc_error** error) {
    GPR_DEBUG_ASSERT(error != nullptr);
    return nullptr;
  }

  virtual UniquePtr<ServiceConfigParsedObject> ParsePerMethodParams(
      const grpc_json* json, grpc_error** error) {
    GPR_DEBUG_ASSERT(error != nullptr);
    return nullptr;
  }

  GRPC_ABSTRACT_BASE_CLASS;
};

class ServiceConfig : public RefCounted<ServiceConfig> {
 public:
  static constexpr int kNumPreallocatedParsers = 4;
  typedef InlinedVector<UniquePtr<ServiceConfigParsedObject>,
                        kNumPreallocatedParsers>
      ServiceConfigObjectsVector;

  class CallData {
   public:
    CallData() = default;
    CallData(RefCountedPtr<ServiceConfig> svc_cfg, const grpc_slice& path)
        : service_config_(std::move(svc_cfg)) {
      if (service_config_ != nullptr) {
        method_params_vector_ =
            service_config_->GetMethodServiceConfigObjectsVector(path);
      }
    }

    RefCountedPtr<ServiceConfig> service_config() { return service_config_; }

    ServiceConfigParsedObject* GetMethodParsedObject(int index) const {
      return method_params_vector_ != nullptr
                 ? (*method_params_vector_)[index].get()
                 : nullptr;
    }

    bool empty() const { return service_config_ == nullptr; }

   private:
    RefCountedPtr<ServiceConfig> service_config_;
    const ServiceConfig::ServiceConfigObjectsVector* method_params_vector_ =
        nullptr;
  };

  /// Creates a new service config from parsing \a json_string.
  /// Returns null on parse error.
  static RefCountedPtr<ServiceConfig> Create(const char* json,
                                             grpc_error** error);

  ~ServiceConfig();

  const char* service_config_json() const { return service_config_json_.get(); }

  /// Retrieves the parsed global service config object at index \a index.
  ServiceConfigParsedObject* GetParsedGlobalServiceConfigObject(size_t index) {
    GPR_DEBUG_ASSERT(index < parsed_global_service_config_objects_.size());
    return parsed_global_service_config_objects_[index].get();
  }

  /// Retrieves the vector of method service config objects for a given path \a
  /// path.
  const ServiceConfigObjectsVector* GetMethodServiceConfigObjectsVector(
      const grpc_slice& path);

  /// Globally register a service config parser. On successful registration, it
  /// returns the index at which the parser was registered. On failure, -1 is
  /// returned. Each new service config update will go through all the
  /// registered parser. Each parser is responsible for reading the service
  /// config json and returning a parsed object. This parsed object can later be
  /// retrieved using the same index that was returned at registration time.
  static size_t RegisterParser(UniquePtr<ServiceConfigParser> parser);

  static void Init();

  static void Shutdown();

  // Consumes all the errors in the vector and forms a referencing error from
  // them. If the vector is empty, return GRPC_ERROR_NONE.
  template <size_t N>
  static grpc_error* CreateErrorFromVector(
      const char* desc, InlinedVector<grpc_error*, N>* error_list) {
    grpc_error* error = GRPC_ERROR_NONE;
    if (error_list->size() != 0) {
      error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          desc, error_list->data(), error_list->size());
      // Remove refs to all errors in error_list.
      for (size_t i = 0; i < error_list->size(); i++) {
        GRPC_ERROR_UNREF((*error_list)[i]);
      }
      error_list->clear();
    }
    return error;
  }

 private:
  // So New() can call our private ctor.
  template <typename T, typename... Args>
  friend T* New(Args&&... args);

  // Takes ownership of \a json_tree.
  ServiceConfig(UniquePtr<char> service_config_json,
                UniquePtr<char> json_string, grpc_json* json_tree,
                grpc_error** error);

  // Helper functions to parse the service config
  grpc_error* ParseGlobalParams(const grpc_json* json_tree);
  grpc_error* ParsePerMethodParams(const grpc_json* json_tree);

  // Returns the number of names specified in the method config \a json.
  static int CountNamesInMethodConfig(grpc_json* json);

  // Returns a path string for the JSON name object specified by \a json.
  // Returns null on error, and stores error in \a error.
  static UniquePtr<char> ParseJsonMethodName(grpc_json* json,
                                             grpc_error** error);

  grpc_error* ParseJsonMethodConfigToServiceConfigObjectsTable(
      const grpc_json* json,
      SliceHashTable<const ServiceConfigObjectsVector*>::Entry* entries,
      size_t* idx);

  UniquePtr<char> service_config_json_;
  UniquePtr<char> json_string_;  // Underlying storage for json_tree.
  grpc_json* json_tree_;

  InlinedVector<UniquePtr<ServiceConfigParsedObject>, kNumPreallocatedParsers>
      parsed_global_service_config_objects_;
  // A map from the method name to the service config objects vector. Note that
  // we are using a raw pointer and not a unique pointer so that we can use the
  // same vector for multiple names.
  RefCountedPtr<SliceHashTable<const ServiceConfigObjectsVector*>>
      parsed_method_service_config_objects_table_;
  // Storage for all the vectors that are being used in
  // parsed_method_service_config_objects_table_.
  InlinedVector<UniquePtr<ServiceConfigObjectsVector>, 32>
      service_config_objects_vectors_storage_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVICE_CONFIG_H */
