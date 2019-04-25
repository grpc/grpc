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

class ServiceConfig : public RefCounted<ServiceConfig> {
 public:
  /// Creates a new service config from parsing \a json_string.
  /// Returns null on parse error.
  static RefCountedPtr<ServiceConfig> Create(const char* json);

  ~ServiceConfig();

  const char* service_config_json() const { return service_config_json_.get(); }

  /// Invokes \a process_json() for each global parameter in the service
  /// config.  \a arg is passed as the second argument to \a process_json().
  template <typename T>
  using ProcessJson = void (*)(const grpc_json*, T*);
  template <typename T>
  void ParseGlobalParams(ProcessJson<T> process_json, T* arg) const;

  /// Gets the LB policy name from \a service_config.
  /// Returns NULL if no LB policy name was specified.
  /// Caller does NOT take ownership.
  const char* GetLoadBalancingPolicyName() const;

  /// Creates a method config table based on the data in \a json.
  /// The table's keys are request paths.  The table's value type is
  /// returned by \a create_value(), based on data parsed from the JSON tree.
  /// Returns null on error.
  template <typename T>
  using CreateValue = RefCountedPtr<T> (*)(const grpc_json* method_config_json);
  template <typename T>
  RefCountedPtr<SliceHashTable<RefCountedPtr<T>>> CreateMethodConfigTable(
      CreateValue<T> create_value) const;

  /// A helper function for looking up values in the table returned by
  /// \a CreateMethodConfigTable().
  /// Gets the method config for the specified \a path, which should be of
  /// the form "/service/method".
  /// Returns null if the method has no config.
  /// Caller does NOT own a reference to the result.
  template <typename T>
  static RefCountedPtr<T> MethodConfigTableLookup(
      const SliceHashTable<RefCountedPtr<T>>& table, const grpc_slice& path);

 private:
  // So New() can call our private ctor.
  template <typename T, typename... Args>
  friend T* New(Args&&... args);

  // Takes ownership of \a json_tree.
  ServiceConfig(UniquePtr<char> service_config_json,
                UniquePtr<char> json_string, grpc_json* json_tree);

  // Returns the number of names specified in the method config \a json.
  static int CountNamesInMethodConfig(grpc_json* json);

  // Returns a path string for the JSON name object specified by \a json.
  // Returns null on error.
  static UniquePtr<char> ParseJsonMethodName(grpc_json* json);

  // Parses the method config from \a json.  Adds an entry to \a entries for
  // each name found, incrementing \a idx for each entry added.
  // Returns false on error.
  template <typename T>
  static bool ParseJsonMethodConfig(
      grpc_json* json, CreateValue<T> create_value,
      typename SliceHashTable<RefCountedPtr<T>>::Entry* entries, size_t* idx);

  UniquePtr<char> service_config_json_;
  UniquePtr<char> json_string_;  // Underlying storage for json_tree.
  grpc_json* json_tree_;
};

//
// implementation -- no user-serviceable parts below
//

template <typename T>
void ServiceConfig::ParseGlobalParams(ProcessJson<T> process_json,
                                      T* arg) const {
  if (json_tree_->type != GRPC_JSON_OBJECT || json_tree_->key != nullptr) {
    return;
  }
  for (grpc_json* field = json_tree_->child; field != nullptr;
       field = field->next) {
    if (field->key == nullptr) return;
    if (strcmp(field->key, "methodConfig") == 0) continue;
    process_json(field, arg);
  }
}

template <typename T>
bool ServiceConfig::ParseJsonMethodConfig(
    grpc_json* json, CreateValue<T> create_value,
    typename SliceHashTable<RefCountedPtr<T>>::Entry* entries, size_t* idx) {
  // Construct value.
  RefCountedPtr<T> method_config = create_value(json);
  if (method_config == nullptr) return false;
  // Construct list of paths.
  InlinedVector<UniquePtr<char>, 10> paths;
  for (grpc_json* child = json->child; child != nullptr; child = child->next) {
    if (child->key == nullptr) continue;
    if (strcmp(child->key, "name") == 0) {
      if (child->type != GRPC_JSON_ARRAY) return false;
      for (grpc_json* name = child->child; name != nullptr; name = name->next) {
        UniquePtr<char> path = ParseJsonMethodName(name);
        if (path == nullptr) return false;
        paths.push_back(std::move(path));
      }
    }
  }
  if (paths.size() == 0) return false;  // No names specified.
  // Add entry for each path.
  for (size_t i = 0; i < paths.size(); ++i) {
    entries[*idx].key = grpc_slice_from_copied_string(paths[i].get());
    entries[*idx].value = method_config;  // Takes a new ref.
    ++*idx;
  }
  // Success.
  return true;
}

template <typename T>
RefCountedPtr<SliceHashTable<RefCountedPtr<T>>>
ServiceConfig::CreateMethodConfigTable(CreateValue<T> create_value) const {
  // Traverse parsed JSON tree.
  if (json_tree_->type != GRPC_JSON_OBJECT || json_tree_->key != nullptr) {
    return nullptr;
  }
  size_t num_entries = 0;
  typename SliceHashTable<RefCountedPtr<T>>::Entry* entries = nullptr;
  for (grpc_json* field = json_tree_->child; field != nullptr;
       field = field->next) {
    if (field->key == nullptr) return nullptr;
    if (strcmp(field->key, "methodConfig") == 0) {
      if (entries != nullptr) return nullptr;  // Duplicate.
      if (field->type != GRPC_JSON_ARRAY) return nullptr;
      // Find number of entries.
      for (grpc_json* method = field->child; method != nullptr;
           method = method->next) {
        int count = CountNamesInMethodConfig(method);
        if (count <= 0) return nullptr;
        num_entries += static_cast<size_t>(count);
      }
      // Populate method config table entries.
      entries = static_cast<typename SliceHashTable<RefCountedPtr<T>>::Entry*>(
          gpr_zalloc(num_entries *
                     sizeof(typename SliceHashTable<RefCountedPtr<T>>::Entry)));
      size_t idx = 0;
      for (grpc_json* method = field->child; method != nullptr;
           method = method->next) {
        if (!ParseJsonMethodConfig(method, create_value, entries, &idx)) {
          for (size_t i = 0; i < idx; ++i) {
            grpc_slice_unref_internal(entries[i].key);
            entries[i].value.reset();
          }
          gpr_free(entries);
          return nullptr;
        }
      }
      GPR_ASSERT(idx == num_entries);
    }
  }
  // Instantiate method config table.
  RefCountedPtr<SliceHashTable<RefCountedPtr<T>>> method_config_table;
  if (entries != nullptr) {
    method_config_table =
        SliceHashTable<RefCountedPtr<T>>::Create(num_entries, entries, nullptr);
    gpr_free(entries);
  }
  return method_config_table;
}

template <typename T>
RefCountedPtr<T> ServiceConfig::MethodConfigTableLookup(
    const SliceHashTable<RefCountedPtr<T>>& table, const grpc_slice& path) {
  const RefCountedPtr<T>* value = table.Get(path);
  // If we didn't find a match for the path, try looking for a wildcard
  // entry (i.e., change "/service/method" to "/service/*").
  if (value == nullptr) {
    char* path_str = grpc_slice_to_c_string(path);
    const char* sep = strrchr(path_str, '/') + 1;
    const size_t len = (size_t)(sep - path_str);
    char* buf = (char*)gpr_malloc(len + 2);  // '*' and NUL
    memcpy(buf, path_str, len);
    buf[len] = '*';
    buf[len + 1] = '\0';
    grpc_slice wildcard_path = grpc_slice_from_copied_string(buf);
    gpr_free(buf);
    value = table.Get(wildcard_path);
    grpc_slice_unref_internal(wildcard_path);
    gpr_free(path_str);
    if (value == nullptr) return nullptr;
  }
  return RefCountedPtr<T>(*value);
}

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVICE_CONFIG_H */
