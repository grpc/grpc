/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "call.h"
#include "timeval.h"
#include "completion_queue.h"

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"

namespace HPHP {

Call::Call() {}
Call::~Call() { sweep(); }

void Call::init(grpc_call* call) {
  memcpy(wrapped, call, sizeof(grpc_call));
}

void Call::sweep() {
  if (wrapped) {
    if (owned) {
      grpc_call_unref(wrapped);
    }
    req::free(wrapped);
    wrapped = nullptr;
  }
  if (channel) {
    req::free(channel);
    channel = nullptr;
  }
}

grpc_call* Call::getWrapped() {
  return wrapped;
}

bool Call::getOwned() {
  return owned;
}

void Call::setChannel(Channel* channel_) {
  channel = channel_;
}

void Call::setOwned(bool owned_) {
  owned = owned_;
}

void HHVM_METHOD(Call, __construct,
  const Object& channel_obj,
  const String& method,
  const Object& deadline_obj,
  const Variant& host_override /* = null */) {
  auto call = Native::data<Call>(this_);
  auto channel = Native::data<Channel>(channel_obj);
  if (channel->getWrapped() == NULL) {
    throw_invalid_argument("Call cannot be constructed from a closed Channel");
    return;
  }

  call->setChannel(channel);

  auto deadlineTimeval = Native::data<Timeval>(deadline_obj);
  grpc_slice method_slice = grpc_slice_from_copied_string(method.toCppString());
  grpc_slice host_slice = host_override.isNull() ? grpc_empty_slice() :
        grpc_slice_from_copied_string(host_override);
  call->init(grpc_channel_create_call(channel->getWrapped(), NULL, GRPC_PROPAGATE_DEFAULTS,
                             completion_queue, method_slice,
                             host_override != NULL ? &host_slice : NULL,
                             deadlineTimeval->getWrapped(), NULL));

  grpc_slice_unref(method_slice);
  grpc_slice_unref(host_slice);

  call->setOwned(true);
}

Object HHVM_METHOD(Call, startBatch,
  const Array& actions) { // array<int, mixed>
  const Object& resultObj = Object();
  auto call = Native::data<Call>(this_);

  size_t op_num = 0;
  grpc_op ops[8];

  grpc_metadata_array metadata;
  grpc_metadata_array trailing_metadata;
  grpc_metadata_array recv_metadata;
  grpc_metadata_array recv_trailing_metadata;
  grpc_status_code status;
  grpc_slice recv_status_details = grpc_empty_slice();
  grpc_slice send_status_details = grpc_empty_slice();
  grpc_byte_buffer *message;
  int cancelled;
  grpc_call_error error;
  char *message_str;
  size_t message_len;

  grpc_metadata_array_init(&metadata);
  grpc_metadata_array_init(&trailing_metadata);
  grpc_metadata_array_init(&recv_metadata);
  grpc_metadata_array_init(&recv_trailing_metadata);
  memset(ops, 0, sizeof(ops));

  for (ArrayIter iter(valuesArr); iter; ++iter) {
    Variant key = iter.first();
    if (!key.isInteger()) {
      throw_invalid_argument("batch keys must be integers");
      return;
    }

    int index = key.toInt32();

    Variant value = iter.second();

    switch(index) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        if (!value.isArray()) {
          throw_invalid_argument("Expected an array value for the metadata");
          goto cleanup;
        }

        if (!hhvm_create_metadata_array(value.toArray(), &metadata)) {
          throw_invalid_argument("Bad metadata value given");
          goto cleanup;
        }

        ops[op_num].data.send_initial_metadata.count = metadata.count;
        ops[op_num].data.send_initial_metadata.metadata = metadata.metadata;ops[op_num]
        break;
      case GRPC_OP_SEND_MESSAGE:
        if (!value.isDict()) {
          throw_invalid_argument("Expected an array dictionary for send message");
          goto cleanup;
        }

        auto messageDict = value.toDict();
        if (messageDict.exists(String("flags"), true)) {
          auto messageFlags = messageDict[String("flags")];
          if (!messageFlags.isInteger()) {
            throw_invalid_argument("Expected an int for message flags");
            goto cleanup;
          }
          ops[op_num].flags = messageFlags.toInt32() & GRPC_WRITE_USED_MASK;
        }

        if (messageDict.exists(String("message"), true)) {
          auto messageValue = messageDict[String("message")];
          if (!messageValue.isString()) {
            throw_invalid_argument("Expected a string for send message");
            goto cleanup;
          }
          String messageValueString = messageValue.toString();
          ops[op_num].data.send_message.send_message = string_to_byte_buffer(messageValueString.toCppString(),
                                                            messageValueString.size());
        }
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        if (!value.isDict()) {
          throw_invalid_argument("Expected an array dictionary for server status");
          goto cleanup;
        }

        auto statusDict = value.toDict();
        if (statusDict.exists(String("metadata"), true)) {
          auto innerValue = statusDict[String("metadata")];
          if (!innerValue.isArray()) {
            throw_invalid_argument("Expected an array for server status metadata value");
            goto cleanup;
          }

          if (!hhvm_create_metadata_array(innerValue, &trailing_metadata)) {
            throw_invalid_argument("Bad trailing metadata value given");
            goto cleanup;
          }

          ops[op_num].data.send_status_from_server.trailing_metadata =
              trailing_metadata.metadata;
          ops[op_num].data.send_status_from_server.trailing_metadata_count =
              trailing_metadata.count;
        }

        if (!statusDict.exists(String("code"), true)) {
          throw_invalid_argument("Integer status code is required");
        }

        auto innerValue = statusDict[String("code")];

        if (!innerValue.isInteger()) {
          throw_invalid_argument("Status code must be an integer");
          goto cleanup;
        }

        ops[op_num].data.send_status_from_server.status = innerValue.toInt32();

        if (!statusDict.exists(String("details"), true)) {
          throw_invalid_argument("String status details is required");
          goto cleanup;
        }

        auto innerValue = statusDict[String("details")];
        if (!innerValue.isString()) {
          throw_invalid_argument("Status details must be a string");
          goto cleanup;
        }

        send_status_details = grpc_slice_from_copied_string(innerValue.toString().toCppString());
        ops[op_num].data.send_status_from_server.status_details = &send_status_details;
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
        throw_invalid_argument("Unrecognized key in batch");
        goto cleanup;
    }

    ops[op_num].op = (grpc_op_type)index;
    ops[op_num].flags = 0;
    ops[op_num].reserved = NULL;
    op_num++;
  }

  error = grpc_call_start_batch(call->getWrapped(), ops, op_num, call->getWrapped(), NULL);

  if (error != GRPC_CALL_OK) {
    throw_invalid_argument("start_batch was called incorrectly", (int64_t)error);
    goto cleanup;
  }

  grpc_completion_queue_pluck(completion_queue, call->getWrapped),
                                gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

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
        byte_buffer_to_string(message, &message_str, &message_len);
        if (message_str == NULL) {
          resultObj.o_set("message", Variant::nullInit());
        } else {
          resultObj.o_set("message", Variant(String(message_str, message_len)));
        }
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        Object recvStatusObj = Object();
        recvStatusObj.o_set("metadata", grpc_parse_metadata_array(&recv_trailing_metadata));
        recvStatusObj.o_set("code", Variant((int64_t)status));
        char *status_details_text = grpc_slice_to_c_string(recv_status_details);
        recvStatusObj.o_set("details", Variant(String(status_details_text, CopyString)));
        gpr_free(status_details_text);
        resultObj.o_set("status", Variant(recvStatusObj));
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        resultObj.o_set("cancelled", (bool)cancelled);
        break;
      default:
        break;
    }
  }

  cleanup:
    grpc_metadata_array_destroy(&metadata);
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
        PHP_GRPC_FREE_STD_ZVAL(message_str);
      }
    }
    return resultObj;
}

String HHVM_METHOD(Call, getPeer) {
  auto call = Native::data<Call>(this_);
  return String(grpc_call_get_peer(call->getWrapped()), CopyString);
}

void HHVM_METHOD(Call, cancel) {
  auto call = Native::data<Call>(this_);
  grpc_call_cancel(call->getWrapped(), NULL);
}

int64_t HHVM_METHOD(Call, setCredentials,
  const Object& creds_obj) {
  auto callCredentials = Native::data<CallCredentials>(creds_obj);
  auto call = Native::data<Call>(this_);

  grpc_call_error error = GRPC_CALL_ERROR;
  error = grpc_call_set_credentials(call->getWrapped(), callCredentials->getWrapped());

  return (int64_t)error;
}

/* Creates and returns a PHP array object with the data in a
 * grpc_metadata_array. Returns NULL on failure */
Variant grpc_parse_metadata_array(grpc_metadata_array *metadata_array) {
  int count = metadata_array->count;
  grpc_metadata *elements = metadata_array->metadata;

  auto array = Array::CreateDict();

  grpc_metadata *elem;
  for (int i = 0; i < count; i++) {
    elem = &elements[i];

    key_len = GRPC_SLICE_LENGTH(elem->key);
    str_key = req::calloc(key_len + 1, sizeof(char));
    memcpy(str_key, GRPC_SLICE_START_PTR(elem->key), key_len);

    str_val = req::calloc(GRPC_SLICE_LENGTH(elem->value) + 1, sizeof(char));
    memcpy(str_val, GRPC_SLICE_START_PTR(elem->value), GRPC_SLICE_LENGTH(elem->value));

    auto key = String(str_key, key_len, CopyString);
    auto val = String(str_val, GRPC_SLICE_LENGTH(elem->value), CopyString);

    req::free(str_key);
    req::free(str_val);

    if (!array.exists(key, true)) {
      array.set(key, Array::Create(), true);
    }

    auto current = array[key];
    if (!current.isArray()) {
      throw_invalid_argument("Metadata hash somehow contains wrong types.");
      return Variant::NullInit();
    }

    current.append(Variant(val));
  }

  return Variant(array);
}

/* Populates a grpc_metadata_array with the data in a PHP array object.
   Returns true on success and false on failure */
bool hhvm_create_metadata_array(const Array& array, grpc_metadata_array *metadata) {
  grpc_metadata_array_init(metadata);

  for (ArrayIter iter(valuesArr); iter; ++iter) {
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

  metadata->metadata = gpr_malloc(metadata->capacity * sizeof(grpc_metadata));

  for (ArrayIter iter(valuesArr); iter; ++iter) {
    Variant key = iter.first();
    if (!key.isString()) {
      return false;
    }

    if (!grpc_header_key_is_legal(grpc_slice_from_static_string(key.toString().toCppString()))) {
      return false;
    }

    Variant value = iter.second();
    if (!value.isArray()) {
      return false;
    }

    Array innerArray = value.toDict();
    for (ArrayIter iter2(valuesArr); iter2; ++iter2) {
      Variant key2 = iter2.first();
      if (!key2.isString()) {
        return false;
      }
      Variant value2 = iter2.second();
      if (!value2.isString()) {
        return false;
      }

      String value2String = value2.toString();

      metadata->metadata[metadata->count].key = grpc_slice_from_copied_string(key2.toString().toCppString());
      metadata->metadata[metadata->count].value = grpc_slice_from_copied_buffer(value2String.toCppString(), value2String.length());
      metadata->count += 1;
    }
  }

  return true;
}

} // namespace HPHP
