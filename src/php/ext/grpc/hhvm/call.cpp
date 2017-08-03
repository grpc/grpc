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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "common.h"

#include "call.h"
#include "timeval.h"
#include "completion_queue.h"
#include "byte_buffer.h"
#include "call_credentials.h"

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/base/type-variant.h"
#include "hphp/util/process.h"

#include <sys/eventfd.h>
#include <sys/poll.h>

#include <grpc/support/alloc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc.h>

namespace HPHP {

Mutex s_grpc_call_start_batch_mutex;

Class* CallData::s_class = nullptr;
const StaticString CallData::s_className("Grpc\\Call");

IMPLEMENT_GET_CLASS(CallData);

CallData::CallData() {}
CallData::~CallData() { sweep(); }

void CallData::init(grpc_call* call) {
  wrapped = call;
}

void CallData::sweep() {
  if (wrapped) {
    if (owned) {
      grpc_call_unref(wrapped);
    }
    wrapped = nullptr;
  }
  if (channelData) {
    channelData = nullptr;
  }
}

grpc_call* CallData::getWrapped() {
  return wrapped;
}

bool CallData::getOwned() {
  return owned;
}

void CallData::setChannelData(ChannelData* channelData_) {
  channelData = channelData_;
}

void CallData::setOwned(bool owned_) {
  owned = owned_;
}

void CallData::setTimeout(int32_t timeout_) {
  timeout = timeout_;
}

int32_t CallData::getTimeout() {
  return timeout;
}

void HHVM_METHOD(Call, __construct,
  const Object& channel_obj,
  const String& method,
  const Object& deadline_obj,
  const Variant& host_override /* = null */) {
  auto callData = Native::data<CallData>(this_);
  auto channelData = Native::data<ChannelData>(channel_obj);

  if (channelData->getWrapped() == nullptr) {
    SystemLib::throwInvalidArgumentExceptionObject("Call cannot be constructed from a closed Channel");
    return;
  }

  callData->setChannelData(channelData);

  auto deadlineTimevalData = Native::data<TimevalData>(deadline_obj);
  callData->setTimeout(gpr_time_to_millis(gpr_convert_clock_type(deadlineTimevalData->getWrapped(), GPR_TIMESPAN)));

  grpc_slice method_slice = grpc_slice_from_copied_string(method.c_str());
  grpc_slice host_slice = !host_override.isNull() ? grpc_slice_from_copied_string(host_override.toString().c_str())
                              : grpc_empty_slice();
  callData->init(grpc_channel_create_call(channelData->getWrapped(), NULL, GRPC_PROPAGATE_DEFAULTS,
                             CompletionQueue::tl_obj.get()->getQueue(), method_slice,
                             !host_override.isNull() ? &host_slice : NULL,
                             deadlineTimevalData->getWrapped(), NULL));

  grpc_slice_unref(method_slice);
  grpc_slice_unref(host_slice);

  callData->setOwned(true);
}

Object HHVM_METHOD(Call, startBatch,
  const Array& actions) { // array<int, mixed>
  auto resultObj = SystemLib::AllocStdClassObject();

  if (actions.size() > 8) {
    SystemLib::throwInvalidArgumentExceptionObject("actions array must not be longer than 8 operations");
    return resultObj;
  }

  auto callData = Native::data<CallData>(this_);

  size_t op_num = 0;
  grpc_op ops[8];

  grpc_metadata_array metadata;
  grpc_metadata_array trailing_metadata;
  grpc_metadata_array recv_metadata;
  grpc_metadata_array recv_trailing_metadata;
  grpc_status_code status;
  grpc_slice recv_status_details = grpc_empty_slice();
  grpc_slice send_status_details = grpc_empty_slice();
  grpc_byte_buffer *message = nullptr;
  int cancelled;
  grpc_call_error error;
  grpc_event *event_ptr = nullptr;
  grpc_event event;

  int fd = -1;

  grpc_metadata_array_init(&metadata);
  grpc_metadata_array_init(&trailing_metadata);
  grpc_metadata_array_init(&recv_metadata);
  grpc_metadata_array_init(&recv_trailing_metadata);
  memset(ops, 0, sizeof(ops));

  bool expect_send_initial_metadata = false;

  for (ArrayIter iter(actions); iter; ++iter) {
    Variant key = iter.first();
    if (!key.isInteger()) {
      SystemLib::throwInvalidArgumentExceptionObject("batch keys must be integers");
      goto cleanup;
    }

    int index = key.toInt32();

    Variant value = iter.second();

    switch(index) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        if (!value.isArray()) {
          SystemLib::throwInvalidArgumentExceptionObject("Expected an array value for the metadata");
          goto cleanup;
        }

        if (!hhvm_create_metadata_array(value.toArray(), &metadata)) {
          SystemLib::throwInvalidArgumentExceptionObject("Bad metadata value given");
          goto cleanup;
        }

        ops[op_num].data.send_initial_metadata.count = metadata.count;
        ops[op_num].data.send_initial_metadata.metadata = metadata.metadata;
        expect_send_initial_metadata = true;
        break;
      case GRPC_OP_SEND_MESSAGE:
        {
          if (!value.isArray()) {
            SystemLib::throwInvalidArgumentExceptionObject("Expected an array for send message");
            goto cleanup;
          }

          auto messageArr = value.toArray();
          if (messageArr.exists(String("flags"), true)) {
            auto messageFlags = messageArr[String("flags")];
            if (!messageFlags.isInteger()) {
              SystemLib::throwInvalidArgumentExceptionObject("Expected an int for message flags");
              goto cleanup;
            }
            ops[op_num].flags = messageFlags.toInt32() & GRPC_WRITE_USED_MASK;
          }

          if (messageArr.exists(String("message"), true)) {
            auto messageValue = messageArr[String("message")];
            if (!messageValue.isString()) {
              SystemLib::throwInvalidArgumentExceptionObject("Expected a string for send message");
              goto cleanup;
            }
            String messageValueString = messageValue.toString();
            ops[op_num].data.send_message.send_message = string_to_byte_buffer(messageValueString.c_str(),
                                                              messageValueString.size());
          }
        }
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        {
          if (!value.isArray()) {
            SystemLib::throwInvalidArgumentExceptionObject("Expected an array for server status");
            goto cleanup;
          }

          auto statusArr = value.toArray();
          if (statusArr.exists(String("metadata"), true)) {
            auto innerMetadata = statusArr[String("metadata")];
            if (!innerMetadata.isArray()) {
              SystemLib::throwInvalidArgumentExceptionObject("Expected an array for server status metadata value");
              goto cleanup;
            }

            if (!hhvm_create_metadata_array(innerMetadata.toArray(), &trailing_metadata)) {
              SystemLib::throwInvalidArgumentExceptionObject("Bad trailing metadata value given");
              goto cleanup;
            }

            ops[op_num].data.send_status_from_server.trailing_metadata =
                trailing_metadata.metadata;
            ops[op_num].data.send_status_from_server.trailing_metadata_count =
                trailing_metadata.count;
          }

          if (!statusArr.exists(String("code"), true)) {
            SystemLib::throwInvalidArgumentExceptionObject("Integer status code is required");
          }

          auto innerCode = statusArr[String("code")];

          if (!innerCode.isInteger()) {
            SystemLib::throwInvalidArgumentExceptionObject("Status code must be an integer");
            goto cleanup;
          }

          ops[op_num].data.send_status_from_server.status = (grpc_status_code)innerCode.toInt32();

          if (!statusArr.exists(String("details"), true)) {
            SystemLib::throwInvalidArgumentExceptionObject("String status details is required");
            goto cleanup;
          }

          auto innerDetails = statusArr[String("details")];
          if (!innerDetails.isString()) {
            SystemLib::throwInvalidArgumentExceptionObject("Status details must be a string");
            goto cleanup;
          }

          send_status_details = grpc_slice_from_copied_string(innerDetails.toString().c_str());
          ops[op_num].data.send_status_from_server.status_details = &send_status_details;
        }
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        ops[op_num].data.recv_initial_metadata.recv_initial_metadata = &recv_metadata;
        break;
      case GRPC_OP_RECV_MESSAGE:
        ops[op_num].data.recv_message.recv_message = &message;
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        ops[op_num].data.recv_status_on_client.trailing_metadata = &recv_trailing_metadata;
        ops[op_num].data.recv_status_on_client.status = &status;
        ops[op_num].data.recv_status_on_client.status_details = &recv_status_details;
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        ops[op_num].data.recv_close_on_server.cancelled = &cancelled;
        break;
      default:
        SystemLib::throwInvalidArgumentExceptionObject("Unrecognized key in batch");
        goto cleanup;
    }

    ops[op_num].op = (grpc_op_type)index;
    ops[op_num].flags = 0;
    ops[op_num].reserved = NULL;
    op_num++;
  }

  if (expect_send_initial_metadata == true) {
    // We do this above grpc_call_start_batch because it can also sometimes execute the callback
    // from within itself and we need to have the fd setup by that point
    fd = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
    PluginGetMetadataFd::tl_obj.get()->setFd(fd);
  }

  {
    Lock l1(s_grpc_call_start_batch_mutex);
    error = grpc_call_start_batch(callData->getWrapped(), ops, op_num, callData->getWrapped(), NULL);
  }

  if (error != GRPC_CALL_OK) {
    SystemLib::throwInvalidArgumentExceptionObject(folly::sformat("start_batch was called incorrectly: {}", (int)error));
    goto cleanup;
  }

  if (expect_send_initial_metadata == true) {
    // This might look weird but it's required due to the way HHVM works. Each request in HHVM
    // has it's own thread and you cannot run application code on a single request in more than
    // one thread. However gRPC calls call_credentials.cpp:plugin_get_metadata in a different thread
    // in many cases and that violates the thread safety within a request in HHVM and causes segfaults
    // at any reasonable concurrency. Our workaround here is to pass in a file descriptor that can be
    // used by the concurrent thread to send back the parameters to this main thread so we can execute
    // it here.

    cq_pluck_async_params *cq_pluck_params;
    cq_pluck_params = (cq_pluck_async_params *)gpr_zalloc(sizeof(cq_pluck_async_params));
    cq_pluck_params->cq = CompletionQueue::tl_obj.get()->getQueue();
    cq_pluck_params->tag = callData->getWrapped();
    cq_pluck_params->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    cq_pluck_params->reserved = nullptr;
    cq_pluck_params->fd = fd;

    pthread_t cq_pluck_thread_id;
    pthread_create(&cq_pluck_thread_id, NULL, cq_pluck_async, (void *)cq_pluck_params);

    // Block, wait for signal from callback
    struct pollfd fds[] = {
        { fd, POLLIN },
    };
    int r = poll(fds, 1, callData->getTimeout()); // Timeout after N seconds as defined by the application
    if (r > 0) {
      plugin_get_metadata_params *params;
      int numBytes = read(fd, &params, sizeof(plugin_get_metadata_params *));
      PluginGetMetadataFd::tl_obj.get()->setFd(-1);

      if (numBytes > 0 && params != nullptr) {
        plugin_do_get_metadata(params->ptr, params->context, params->cb, params->user_data);
        gpr_free(params);
      }
    }
    pthread_join(cq_pluck_thread_id, (void **)&event_ptr);
    event = *event_ptr;
  } else {
    event = grpc_completion_queue_pluck(CompletionQueue::tl_obj.get()->getQueue(), callData->getWrapped(),
                                        gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  }

  // An error occured if success = 0
  if (event.success == 0) {
    SystemLib::throwInvalidArgumentExceptionObject(folly::sformat("There was a problem with the request. Event error code: {}", (int)event.type));
    goto cleanup;
  }

  for (int i = 0; i < op_num; i++) {
    switch(ops[i].op) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        resultObj.o_set("send_metadata", Variant(true));
        break;
      case GRPC_OP_SEND_MESSAGE:
        resultObj.o_set("send_message", Variant(true));
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        resultObj.o_set("send_close", Variant(true));
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        resultObj.o_set("send_status", Variant(true));
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        resultObj.o_set("metadata", grpc_parse_metadata_array(&recv_metadata));
        break;
      case GRPC_OP_RECV_MESSAGE:
        char *message_str;
        size_t message_len;
        byte_buffer_to_string(message, &message_str, &message_len);
        if (message_str == NULL) {
          resultObj.o_set("message", Variant());
        } else {
          resultObj.o_set("message", Variant(String(message_str, message_len, CopyString)));
        }
        req::free(message_str);
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        {
          auto recvStatusObj = SystemLib::AllocStdClassObject();
          recvStatusObj.o_set("metadata", grpc_parse_metadata_array(&recv_trailing_metadata));
          recvStatusObj.o_set("code", Variant((int64_t)status));
          char *status_details_text = grpc_slice_to_c_string(recv_status_details);
          recvStatusObj.o_set("details", Variant(String(status_details_text, CopyString)));
          gpr_free(status_details_text);
          resultObj.o_set("status", Variant(recvStatusObj));
        }
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        resultObj.o_set("cancelled", (bool)cancelled);
        break;
      default:
        break;
    }
  }

  cleanup:
    if (fd != -1) {
      close(fd);
    }
    if (event_ptr != nullptr) {
      gpr_free(event_ptr);
    }
    for (int i = 0; i < metadata.count; i++) {
      grpc_slice_unref(metadata.metadata[i].key);
      grpc_slice_unref(metadata.metadata[i].value);
    }
    grpc_metadata_array_destroy(&metadata);
    for (int i = 0; i < trailing_metadata.count; i++) {
      grpc_slice_unref(trailing_metadata.metadata[i].key);
      grpc_slice_unref(trailing_metadata.metadata[i].value);
    }
    grpc_metadata_array_destroy(&trailing_metadata);
    grpc_metadata_array_destroy(&recv_metadata);
    grpc_metadata_array_destroy(&recv_trailing_metadata);
    grpc_slice_unref(recv_status_details);
    grpc_slice_unref(send_status_details);
    for (int i = 0; i < op_num; i++) {
      if (ops[i].op == GRPC_OP_SEND_MESSAGE) {
        grpc_byte_buffer_destroy(ops[i].data.send_message.send_message);
      }
      if (ops[i].op == GRPC_OP_RECV_MESSAGE) {
        grpc_byte_buffer_destroy(message);
      }
    }
    return resultObj;
}

void *cq_pluck_async(void *params_ptr) {
  cq_pluck_async_params *params = (cq_pluck_async_params *)params_ptr;
  grpc_event event;
  grpc_event *event_copy = (grpc_event *)gpr_zalloc(sizeof(grpc_event));


  // This doesn't need a lock b/c we use a completion queue per HHVM request thread
  event = grpc_completion_queue_pluck(params->cq, params->tag, params->deadline, params->reserved);

  // An error occured if success = 0
  if (event.success == 0) {
    // If there was an error then our callback wouldn't have been called but the main
    // thread is still blocking on the read, so we need to unblock it somehow. We do
    // this by writing nullptr to the fd which is then handled on the other side.
    write(params->fd, nullptr, sizeof(plugin_get_metadata_params *));
  }

  *event_copy = event;

  return (void *)event_copy;
}


String HHVM_METHOD(Call, getPeer) {
  auto callData = Native::data<CallData>(this_);
  return String(grpc_call_get_peer(callData->getWrapped()), CopyString);
}

void HHVM_METHOD(Call, cancel) {
  auto callData = Native::data<CallData>(this_);
  grpc_call_cancel(callData->getWrapped(), NULL);
}

int64_t HHVM_METHOD(Call, setCredentials,
  const Object& creds_obj) {
  auto callCredentialsData = Native::data<CallCredentialsData>(creds_obj);
  auto callData = Native::data<CallData>(this_);

  grpc_call_error error = GRPC_CALL_ERROR;
  error = grpc_call_set_credentials(callData->getWrapped(), callCredentialsData->getWrapped());

  return (int64_t)error;
}

/* Creates and returns a PHP array object with the data in a
 * grpc_metadata_array. Returns NULL on failure */
Variant grpc_parse_metadata_array(grpc_metadata_array *metadata_array) {
  char *str_key;
  char *str_val;
  size_t key_len;

  int count = metadata_array->count;
  grpc_metadata *elements = metadata_array->metadata;

  auto array = Array::Create();

  grpc_metadata *elem;
  for (int i = 0; i < count; i++) {
    elem = &elements[i];

    key_len = GRPC_SLICE_LENGTH(elem->key);
    #if HHVM_VERSION_MAJOR >= 3 && HHVM_VERSION_MINOR >= 19
    str_key = (char *) req::calloc_untyped(key_len + 1, sizeof(char));
    #else
    str_key = (char *) req::calloc(key_len + 1, sizeof(char));
    #endif
    memcpy(str_key, GRPC_SLICE_START_PTR(elem->key), key_len);

    #if HHVM_VERSION_MAJOR >= 3 && HHVM_VERSION_MINOR >= 19
    str_val = (char *) req::calloc_untyped(GRPC_SLICE_LENGTH(elem->value) + 1, sizeof(char));
    #else
    str_val = (char *) req::calloc(GRPC_SLICE_LENGTH(elem->value) + 1, sizeof(char));
    #endif
    memcpy(str_val, GRPC_SLICE_START_PTR(elem->value), GRPC_SLICE_LENGTH(elem->value));

    auto key = String(str_key, key_len, CopyString);
    auto val = String(str_val, GRPC_SLICE_LENGTH(elem->value), CopyString);

    req::free(str_key);
    req::free(str_val);

    if (!array.exists(key, true)) {
      array.set(key, Array::Create(), true);
    }

    Variant current = array[key];
    if (!current.isArray()) {
      SystemLib::throwInvalidArgumentExceptionObject("Metadata hash somehow contains wrong types.");
      return Variant();
    }

    Array currArray = current.toArray();

    currArray.append(Variant(val));
  }

  return Variant(array);
}

/* Populates a grpc_metadata_array with the data in a PHP array object.
   Returns true on success and false on failure */
bool hhvm_create_metadata_array(const Array& array, grpc_metadata_array *metadata) {
  // Must call grpc_metadata_array_init(&metadata); before calling this function

  for (ArrayIter iter(array); iter; ++iter) {
    Variant key = iter.first();
    if (!key.isString() || key.isNull()) {
      return false;
    }

    Variant value = iter.second();
    if (!value.isArray()) {
      return false;
    }

    metadata->capacity += value.toArray().size();
  }

  metadata->metadata = (grpc_metadata *)gpr_malloc(metadata->capacity * sizeof(grpc_metadata));

  for (ArrayIter iter(array); iter; ++iter) {
    Variant key = iter.first();
    if (!key.isString()) {
      return false;
    }

    if (!grpc_header_key_is_legal(grpc_slice_from_static_string(key.toString().c_str()))) {
      return false;
    }

    Variant value = iter.second();
    if (!value.isArray()) {
      return false;
    }

    Array innerArray = value.toArray();
    for (ArrayIter iter2(innerArray); iter2; ++iter2) {
      Variant key2 = iter2.first();
      Variant value2 = iter2.second();
      if (!value2.isString()) {
        return false;
      }

      String value2String = value2.toString();

      metadata->metadata[metadata->count].key = grpc_slice_from_copied_string(key.toString().c_str());
      metadata->metadata[metadata->count].value = grpc_slice_from_copied_buffer(value2String.c_str(), value2String.length());
      metadata->count += 1;
    }
  }

  return true;
}

} // namespace HPHP
