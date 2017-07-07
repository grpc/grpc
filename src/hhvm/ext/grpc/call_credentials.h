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

#ifndef NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_
#define NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace HPHP {

class CallCredentialsData {
  private:
    grpc_call_credentials* wrapped{nullptr};
  public:
    static Class* s_class;
    static const StaticString s_className;

    static Class* getClass();

    CallCredentialsData();
    ~CallCredentialsData();

    void init(grpc_call_credentials* call_credentials);
    void sweep();
    grpc_call_credentials* getWrapped();
};

typedef struct plugin_get_metadata_params {
  void *ptr;
  grpc_auth_metadata_context context;
  grpc_credentials_plugin_metadata_cb cb;
  void *user_data;
} plugin_get_metadata_params;

class PluginGetMetadataHandler {
  private:
    std::map<pthread_t, plugin_get_metadata_params *> thread_map;
    Mutex thread_map_mutex;
  public:
    static PluginGetMetadataHandler& getInstance() {
      static PluginGetMetadataHandler instance;
      return instance;
    }

    PluginGetMetadataHandler() { }

    void set(pthread_t thread_id, plugin_get_metadata_params *params) {
      // TODO: reduce contention and make this a lock per-request
      Lock l(thread_map_mutex);
      thread_map[thread_id] = params;

      std::map<pthread_t, plugin_get_metadata_params *>::iterator it;
      it = thread_map.find(thread_id);
      if (it == thread_map.end()) {
        return;
      }

      return;
    }

    plugin_get_metadata_params *getAndClear(pthread_t thread_id) {
      Lock l(thread_map_mutex);

      std::map<pthread_t, plugin_get_metadata_params *>::iterator it;
      it = thread_map.find(thread_id);
      if (it == thread_map.end()) {
        return NULL;
      }

      plugin_get_metadata_params *params = it->second;
      thread_map.erase(thread_id);
      return params;
    }

    PluginGetMetadataHandler(PluginGetMetadataHandler const&) = delete;
    void operator=(PluginGetMetadataHandler const&)           = delete;
};

typedef struct plugin_state {
  Variant callback;
  pthread_t req_thread_id;
} plugin_state;

Object HHVM_STATIC_METHOD(CallCredentials, createComposite,
  const Object& cred1_obj,
  const Object& cred2_obj);

Object HHVM_STATIC_METHOD(CallCredentials, createFromPlugin,
  const Variant& callback);

void plugin_do_get_metadata(void *ptr, grpc_auth_metadata_context context,
                          grpc_credentials_plugin_metadata_cb cb,
                         void *user_data);
void plugin_get_metadata(void *ptr, grpc_auth_metadata_context context,
                         grpc_credentials_plugin_metadata_cb cb,
                         void *user_data);
void plugin_destroy_state(void *ptr);

}

#endif /* NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_ */
