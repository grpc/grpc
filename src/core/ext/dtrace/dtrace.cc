/*
 *
 * Copyright 2022 gRPC authors.
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

#include "src/core/ext/dtrace/dtrace.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/transport/transport_impl.h"

#include <vector>

#ifdef GRPC_DTRACE
#include "dtrace_provider.h"
#else
#define GRPC_TRANSPORT_SEND_INITIAL_METADATA_ENABLED() (0)
#define GRPC_TRANSPORT_SEND_INITIAL_METADATA(...)
#define GRPC_TRANSPORT_SEND_MESSAGE_ENABLED() (0)
#define GRPC_TRANSPORT_SEND_MESSAGE(...)
#define GRPC_TRANSPORT_SEND_TRAILING_METADATA_ENABLED() (0)
#define GRPC_TRANSPORT_SEND_TRAILING_METADATA(...)
#define GRPC_TRANSPORT_RECV_INITIAL_METADATA_ENABLED() (0)
#define GRPC_TRANSPORT_RECV_INITIAL_METADATA(...)
#define GRPC_TRANSPORT_RECV_MESSAGE_ENABLED() (0)
#define GRPC_TRANSPORT_RECV_MESSAGE(...)
#define GRPC_TRANSPORT_RECV_TRAILING_METADATA_ENABLED() (0)
#define GRPC_TRANSPORT_RECV_TRAILING_METADATA(...)
#define GRPC_TRANSPORT_CANCEL_STREAM_ENABLED() (0)
#define GRPC_TRANSPORT_CANCEL_STREAM(...)
#endif

/* Layout-identical to `dtrace_provider.d` */
typedef struct {
  const char *key;
  const char *value;
} grpc_transport_metadata;

/* Layout-identical to `dtrace_provider.d` */
typedef struct {
  void *opaque;
  const char *transport;
  const char *load_address;
  const char *peer_address;
} grpc_transport_stream;

class DTraceStringStore {
public:
  const char *Store(absl::string_view s) {
    std::vector<char> v;
    v.insert(std::end(v), std::begin(s), std::end(s));
    v.insert(std::end(v), '\0');

    store_.emplace_back(std::move(v));
    return store_.back().data();
  }

private:
  // Storing this way is weirder than using std::vector<std::string>,
  // but for some reason std::string storage for short strings causes issues when reading
  // said data from eBPF. This does not happen for short strings when stored in std::vector<char>.
  // Storing as a plain std::vector<char> will cause resizes, which we'd like to avoid.
  std::vector<std::vector<char>> store_;
};

class DTraceStreamRegistry {
public:
  const grpc_transport_stream *Register(grpc_transport *transport, grpc_stream *stream) {
    auto &registeree = streams_.emplace(stream, nullptr).first->second;
    if (registeree == nullptr) {
      auto ep = grpc_transport_get_endpoint(transport);
      registeree = std::make_unique<grpc_transport_stream>();
      registeree->transport = transport->vtable->name;
      registeree->load_address = string_store_.Store(grpc_endpoint_get_local_address(ep));
      registeree->peer_address = string_store_.Store(grpc_endpoint_get_peer(ep));
    }

    return registeree.get();
  }

  void Unregister(grpc_stream *stream) {
    streams_.erase(stream);
  }

private:
  std::map<grpc_stream *, std::unique_ptr<grpc_transport_stream>> streams_;
  DTraceStringStore string_store_;
} g_stream_registry;

class DTraceMetadataEncoder {
 public:

  const void *data() const { return encoded_.data(); }
  size_t size() const { return encoded_.size(); }

  void Encode(const grpc_core::Slice& key, const grpc_core::Slice& value) {
    encoded_.emplace_back(grpc_transport_metadata{
      string_store_.Store(key.as_string_view()),
      string_store_.Store(value.as_string_view())});
  }

  template<typename Key, typename Value>
  void Encode(Key, const Value &value) {
    encoded_.emplace_back(grpc_transport_metadata{
      Key::key().data(), string_store_.Store(Key::Encode(value).as_string_view())});
  }

 private:
  std::vector<grpc_transport_metadata> encoded_;
  DTraceStringStore string_store_;
};

void grpc_dtrace_transport_on_stream_created(grpc_transport* transport,
                                             grpc_stream* stream)
{
  if (!GRPC_TRANSPORT_STREAM_CREATED_ENABLED())
    return;

  GRPC_TRANSPORT_STREAM_CREATED(g_stream_registry.Register(transport, stream));
}

void grpc_dtrace_transport_on_stream_destroyed(grpc_transport* transport,
                                               grpc_stream* stream)
{
  if (GRPC_TRANSPORT_STREAM_DESTROYED_ENABLED()) {
    GRPC_TRANSPORT_STREAM_DESTROYED(g_stream_registry.Register(transport, stream));
  }

  // We don't have to register streams, but we have to unregister them
  // to avoid stale pointers.
  g_stream_registry.Unregister(stream);
}

typedef struct {
  const grpc_transport_stream *stream;
  grpc_transport_stream_op_batch* op;
  grpc_closure * current;
  grpc_closure * prev;
} recv_closure_context;

static void recv_initial_metadata_closure(void* opaque, grpc_error_handle error) {
  auto context = reinterpret_cast<recv_closure_context*>(opaque);
  DTraceMetadataEncoder encoder;
  
  if (context->op->payload && context->op->payload->recv_initial_metadata.recv_initial_metadata) {
    context->op->payload->recv_initial_metadata.recv_initial_metadata->Encode(&encoder);
  }

  GRPC_TRANSPORT_RECV_INITIAL_METADATA(context->stream, encoder.data(), encoder.size());
  
  if (context->prev)
    grpc_core::Closure::Run(DEBUG_LOCATION, context->prev, error);

  delete context;
};

static void recv_message_closure(void* opaque, grpc_error_handle error) {
  auto context = reinterpret_cast<recv_closure_context*>(opaque);
  DTraceMetadataEncoder encoder;
  std::vector<uint8_t> data;

  if (context->op->payload && context->op->payload->recv_message.recv_message &&
      context->op->payload->recv_message.recv_message->has_value()) {
    const auto & message = context->op->payload->recv_message.recv_message->value();
    data.resize(message.Length());
    message.CopyFirstNBytesIntoBuffer(data.size(), data.data());
  }

  GRPC_TRANSPORT_RECV_MESSAGE(context->stream, data.data(), data.size());
  
  if (context->prev)
    grpc_core::Closure::Run(DEBUG_LOCATION, context->prev, error);

  delete context;
};

static void recv_trailing_metadata_closure(void* opaque, grpc_error_handle error) {
  auto context = reinterpret_cast<recv_closure_context*>(opaque);
  DTraceMetadataEncoder encoder;
    
  if (context->op->payload && context->op->payload->recv_trailing_metadata.recv_trailing_metadata) {
    context->op->payload->recv_trailing_metadata.recv_trailing_metadata->Encode(&encoder);
  }

  GRPC_TRANSPORT_RECV_TRAILING_METADATA(context->stream, encoder.data(), encoder.size());

  if (context->prev)
    grpc_core::Closure::Run(DEBUG_LOCATION, context->prev, error);

  delete context;
};

void grpc_dtrace_transport_on_perform_stream_op(grpc_transport* transport,
                                                grpc_stream* stream,
                                                grpc_transport_stream_op_batch* op) {
  if (GRPC_TRANSPORT_SEND_INITIAL_METADATA_ENABLED() && op->send_initial_metadata) {
    DTraceMetadataEncoder encoder;
    
    if (op->payload && op->payload->send_initial_metadata.send_initial_metadata) {
      op->payload->send_initial_metadata.send_initial_metadata->Encode(&encoder);
    }

    GRPC_TRANSPORT_SEND_INITIAL_METADATA(g_stream_registry.Register(transport, stream),
      encoder.data(), encoder.size());
  }

  if (GRPC_TRANSPORT_SEND_MESSAGE_ENABLED() && op->send_message) {
    std::vector<uint8_t> data;

    if (op->payload && op->payload->send_message.send_message) {
      const auto & message = *op->payload->send_message.send_message;
      data.resize(message.Length());
      message.CopyFirstNBytesIntoBuffer(data.size(), data.data());
    }

    GRPC_TRANSPORT_SEND_MESSAGE(g_stream_registry.Register(transport, stream),
      data.data(), data.size());
  }

  if (GRPC_TRANSPORT_SEND_TRAILING_METADATA_ENABLED() && op->send_trailing_metadata) {
    DTraceMetadataEncoder encoder;
    
    if (op->payload && op->payload->send_trailing_metadata.send_trailing_metadata) {
      op->payload->send_trailing_metadata.send_trailing_metadata->Encode(&encoder);
    }

    GRPC_TRANSPORT_SEND_TRAILING_METADATA(g_stream_registry.Register(transport, stream),
      encoder.data(), encoder.size());
  }

  if (GRPC_TRANSPORT_RECV_INITIAL_METADATA_ENABLED() && op->recv_initial_metadata) {
    if (op->payload) {
      auto context = new recv_closure_context;

      context->stream = g_stream_registry.Register(transport, stream);
      context->op = op;
      context->current = GRPC_CLOSURE_CREATE(recv_initial_metadata_closure, context, nullptr);
      context->prev = op->payload->recv_initial_metadata.recv_initial_metadata_ready;
      op->payload->recv_initial_metadata.recv_initial_metadata_ready = context->current;
    }
  }

  if (GRPC_TRANSPORT_RECV_MESSAGE_ENABLED() && op->recv_message) {
    if (op->payload) {
      auto context = new recv_closure_context;

      context->stream = g_stream_registry.Register(transport, stream);
      context->op = op;
      context->current = GRPC_CLOSURE_CREATE(recv_message_closure, context, nullptr);
      context->prev = op->payload->recv_message.recv_message_ready;
      op->payload->recv_message.recv_message_ready = context->current;
    }
  }

  if (GRPC_TRANSPORT_RECV_TRAILING_METADATA_ENABLED() && op->recv_trailing_metadata) {
    if (op->payload) {
      auto context = new recv_closure_context;

      context->stream = g_stream_registry.Register(transport, stream);
      context->op = op;
      context->current = GRPC_CLOSURE_CREATE(recv_trailing_metadata_closure, context, nullptr);
      context->prev = op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
      op->payload->recv_trailing_metadata.recv_trailing_metadata_ready = context->current;
    }
  }

  if (GRPC_TRANSPORT_CANCEL_STREAM_ENABLED() && op->cancel_stream) {
    const char *message = nullptr;
    int status = -1;

    if (op->payload) {
      status = static_cast<int>(op->payload->cancel_stream.cancel_error.code());
      message = op->payload->cancel_stream.cancel_error.message().data();
    }
    GRPC_TRANSPORT_CANCEL_STREAM(g_stream_registry.Register(transport, stream), status, message);
  }
}
