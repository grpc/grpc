/*
 *
 * Copyright 2016 gRPC authors.
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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/metadata.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"
#include "test/core/util/passthru_endpoint.h"

////////////////////////////////////////////////////////////////////////////////
// logging

bool squelch = true;
bool leak_check = true;

static void dont_log(gpr_log_func_args* /*args*/) {}

////////////////////////////////////////////////////////////////////////////////
// global state

static gpr_timespec g_now;
static grpc_server* g_server;
static grpc_channel* g_channel;
static grpc_resource_quota* g_resource_quota;

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

static gpr_timespec now_impl(gpr_clock_type clock_type) {
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  gpr_timespec ts = g_now;
  ts.clock_type = clock_type;
  return ts;
}

////////////////////////////////////////////////////////////////////////////////
// dns resolution

typedef struct addr_req {
  grpc_timer timer;
  char* addr;
  grpc_closure* on_done;
  grpc_resolved_addresses** addrs;
  std::unique_ptr<grpc_core::ServerAddressList>* addresses;
} addr_req;

static void finish_resolve(void* arg, grpc_error* error) {
  addr_req* r = static_cast<addr_req*>(arg);

  if (error == GRPC_ERROR_NONE && 0 == strcmp(r->addr, "server")) {
    if (r->addrs != nullptr) {
      grpc_resolved_addresses* addrs =
          static_cast<grpc_resolved_addresses*>(gpr_malloc(sizeof(*addrs)));
      addrs->naddrs = 1;
      addrs->addrs = static_cast<grpc_resolved_address*>(
          gpr_malloc(sizeof(*addrs->addrs)));
      addrs->addrs[0].len = 0;
      *r->addrs = addrs;
    } else if (r->addresses != nullptr) {
      *r->addresses = absl::make_unique<grpc_core::ServerAddressList>();
      grpc_resolved_address fake_resolved_address;
      memset(&fake_resolved_address, 0, sizeof(fake_resolved_address));
      fake_resolved_address.len = 0;
      (*r->addresses)->emplace_back(fake_resolved_address, nullptr);
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, r->on_done, GRPC_ERROR_NONE);
  } else {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, r->on_done,
                            GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                "Resolution failed", &error, 1));
  }

  gpr_free(r->addr);
  delete r;
}

void my_resolve_address(const char* addr, const char* /*default_port*/,
                        grpc_pollset_set* /*interested_parties*/,
                        grpc_closure* on_done,
                        grpc_resolved_addresses** addrs) {
  addr_req* r = new addr_req();
  r->addr = gpr_strdup(addr);
  r->on_done = on_done;
  r->addrs = addrs;
  grpc_timer_init(
      &r->timer, GPR_MS_PER_SEC + grpc_core::ExecCtx::Get()->Now(),
      GRPC_CLOSURE_CREATE(finish_resolve, r, grpc_schedule_on_exec_ctx));
}

static grpc_address_resolver_vtable fuzzer_resolver = {my_resolve_address,
                                                       nullptr};

grpc_ares_request* my_dns_lookup_ares_locked(
    const char* /*dns_server*/, const char* addr, const char* /*default_port*/,
    grpc_pollset_set* /*interested_parties*/, grpc_closure* on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addresses,
    std::unique_ptr<grpc_core::ServerAddressList>* /*balancer_addresses*/,
    char** /*service_config_json*/, int /*query_timeout*/,
    std::shared_ptr<grpc_core::WorkSerializer> /*combiner*/) {
  addr_req* r = new addr_req();
  r->addr = gpr_strdup(addr);
  r->on_done = on_done;
  r->addrs = nullptr;
  r->addresses = addresses;
  grpc_timer_init(
      &r->timer, GPR_MS_PER_SEC + grpc_core::ExecCtx::Get()->Now(),
      GRPC_CLOSURE_CREATE(finish_resolve, r, grpc_schedule_on_exec_ctx));
  return nullptr;
}

static void my_cancel_ares_request_locked(grpc_ares_request* request) {
  GPR_ASSERT(request == nullptr);
}

////////////////////////////////////////////////////////////////////////////////
// client connection

static void sched_connect(grpc_closure* closure,
                          grpc_slice_allocator* slice_allocator,
                          grpc_endpoint** ep, gpr_timespec deadline);

typedef struct {
  grpc_timer timer;
  grpc_closure* closure;
  grpc_endpoint** ep;
  gpr_timespec deadline;
  grpc_slice_allocator* slice_allocator;
} future_connect;

static void do_connect(void* arg, grpc_error* error) {
  future_connect* fc = static_cast<future_connect*>(arg);
  if (error != GRPC_ERROR_NONE) {
    grpc_slice_allocator_destroy(fc->slice_allocator);
    *fc->ep = nullptr;
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, fc->closure, GRPC_ERROR_REF(error));
  } else if (g_server != nullptr) {
    grpc_slice_allocator_destroy(fc->slice_allocator);
    grpc_endpoint* client;
    grpc_endpoint* server;
    grpc_passthru_endpoint_create(&client, &server, nullptr);
    *fc->ep = client;

    grpc_transport* transport = grpc_create_chttp2_transport(
        nullptr, server, false,
        grpc_resource_user_create(g_resource_quota, "transport-user"));
    g_server->core_server->SetupTransport(transport, nullptr, nullptr, nullptr);
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);

    grpc_core::ExecCtx::Run(DEBUG_LOCATION, fc->closure, GRPC_ERROR_NONE);
  } else {
    sched_connect(fc->closure, fc->slice_allocator, fc->ep, fc->deadline);
  }
  gpr_free(fc);
}

static void sched_connect(grpc_closure* closure,
                          grpc_slice_allocator* slice_allocator,
                          grpc_endpoint** ep, gpr_timespec deadline) {
  if (gpr_time_cmp(deadline, gpr_now(deadline.clock_type)) < 0) {
    *ep = nullptr;
    grpc_slice_allocator_destroy(slice_allocator);
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, closure,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Connect deadline exceeded"));
    return;
  }

  future_connect* fc = static_cast<future_connect*>(gpr_malloc(sizeof(*fc)));
  fc->closure = closure;
  fc->ep = ep;
  fc->deadline = deadline;
  fc->slice_allocator = slice_allocator;
  grpc_timer_init(
      &fc->timer, GPR_MS_PER_SEC + grpc_core::ExecCtx::Get()->Now(),
      GRPC_CLOSURE_CREATE(do_connect, fc, grpc_schedule_on_exec_ctx));
}

static void my_tcp_client_connect(grpc_closure* closure, grpc_endpoint** ep,
                                  grpc_slice_allocator* slice_allocator,
                                  grpc_pollset_set* /*interested_parties*/,
                                  const grpc_channel_args* /*channel_args*/,
                                  const grpc_resolved_address* /*addr*/,
                                  grpc_millis deadline) {
  sched_connect(closure, slice_allocator, ep,
                grpc_millis_to_timespec(deadline, GPR_CLOCK_MONOTONIC));
}

grpc_tcp_client_vtable fuzz_tcp_client_vtable = {my_tcp_client_connect};

////////////////////////////////////////////////////////////////////////////////
// test driver

class Validator {
 public:
  Validator(std::function<void(bool)> impl) : impl_(impl) {}

  virtual ~Validator() {}
  void Run(bool success) {
    impl_(success);
    delete this;
  }

 private:
  std::function<void(bool)> impl_;
};

Validator* MakeValidator(std::function<void(bool)> impl) {
  return new Validator(std::move(impl));
}

static Validator* AssertSuccessAndDecrement(int* counter) {
  return MakeValidator([counter](bool success) {
    GPR_ASSERT(success);
    --*counter;
  });
}

static Validator* Decrement(int* counter) {
  return MakeValidator([counter](bool) { --*counter; });
}

static Validator* ValidateConnectivityWatch(gpr_timespec deadline,
                                            int* counter) {
  return MakeValidator([deadline, counter](bool success) {
    if (!success) {
      GPR_ASSERT(gpr_time_cmp(gpr_now(w->deadline.clock_type), w->deadline) >=
                 0);
    }
    --*counter;
  };
}

static void free_non_null(void* p) {
  GPR_ASSERT(p != nullptr);
  gpr_free(p);
}

enum class CallType { CLIENT, SERVER, PENDING_SERVER };

class Call {
 public:
  Call(CallType type) : type_(type) {}

  ~Call();

  bool done() const {
    if (call_ == nullptr) return true;
    if (pending_ops_ == 0) return true;
    return false;
  }

  void* Allocate(size_t size) {
    void* p = gpr_malloc(size);
    free_pointers_.push_back(p);
    return p;
  }

  template <typename T>
  T* AllocArray(size_t elems) {
    return static_cast<T*>(Allocate(sizeof(T) * elems));
  }

  template <typename T>
  grpc_slice ReadSlice(const T& s) {
    grpc_slice slice = grpc_slice_from_cpp_string(s.value());
    if (s.intern()) {
      slice = grpc_slice_intern(slice);
    }
    unref_slices_.push_back(slice);
    return slice;
  }

  template <typename M>
  grpc_metadata_array ReadMetadata(const M& metadata) {
    grpc_metadata* m = AllocArray<grpc_metadata>(metadata.size());
    for (size_t i = 0; i < metadata.size(); i++) {
      m[i].key = ReadSlice(metadata[i].key);
      m[i].value = ReadSlice(metadata[i].value);
    }
    return grpc_metadata_array{metadata.size(), metadata.size(), m};
  }

  Validator* FinishedBatchValidator(uint8_t has_ops) {
    return MakeValidator([this, has_ops](bool) {
      --pending_ops_;
      if ((has_ops & (1u << GRPC_OP_RECV_MESSAGE)) && call_closed_) {
        GPR_ASSERT(recv_message_ == nullptr);
      }
      if ((has_ops & (1u << GRPC_OP_RECV_MESSAGE) &&
           recv_message_ != nullptr)) {
        grpc_byte_buffer_destroy(recv_message_);
        recv_message_ = nullptr;
      }
      if ((has_ops & (1u << GRPC_OP_SEND_MESSAGE))) {
        grpc_byte_buffer_destroy(send_message_);
        send_message_ = nullptr;
      }
      if ((has_ops & (1u << GRPC_OP_RECV_STATUS_ON_CLIENT)) ||
          (has_ops & (1u << GRPC_OP_RECV_CLOSE_ON_SERVER))) {
        done_flags |= DONE_FLAG_CALL_CLOSED;
      }
    });
  }

 private:
  const CallType type_;
  grpc_call* call_;
  grpc_byte_buffer* recv_message_;
  grpc_status_code status_;
  grpc_metadata_array recv_initial_metadata_;
  grpc_metadata_array recv_trailing_metadata_;
  grpc_slice recv_status_details_ = grpc_empty_slice();
  bool cancelled_ = false;
  int pending_ops_ = 0;
  bool sent_initial_metadata_ = false;
  grpc_call_details call_details_{};
  grpc_byte_buffer* send_message;
  bool call_closed_ = false;

  std::vector<void*> free_pointers_;
  std::vector<grpc_slice> unref_slices_;
};

static std::vector<std::unique_ptr<Call>> g_calls;
static size_t g_active_call = 0;

static Call* ActiveCall() {
  while (g_calls.size()) {
    if (g_active_call >= g_calls.size()) {
      g_active_call = 0;
    }
    if (!g_calls[g_active_call]->done()) {
      return g_calls[g_active_call].get();
    }
    g_calls.erase(g_calls.begin() + g_active_call);
  }
  return nullptr;
}

Call::~Call() {
  if (call_ != nullptr) {
    grpc_call_unref(call_);
  }

  grpc_slice_unref(recv_status_details_);
  grpc_call_details_destroy(&call_details_);

  for (auto* p : free_pointers_) {
    gpr_free(p);
  }
  for (auto s : unref_slices_) {
    grpc_slice_unref(s);
  }
}

static void finished_request_call(void* csp, bool success) {
  call_state* cs = static_cast<call_state*>(csp);
  GPR_ASSERT(cs->pending_ops > 0);
  --cs->pending_ops;
  if (success) {
    GPR_ASSERT(cs->call != nullptr);
    cs->type = SERVER;
  } else {
    maybe_delete_call_state(cs);
  }
}

grpc_channel_args* ReadArgs(
    const google::protobuf::RepeatedPtrField<api_fuzzer::ChannelArg>& args) {
  grpc_channel_args* res =
      static_cast<grpc_channel_args*>(gpr_malloc(sizeof(grpc_channel_args)));
  res->num_args = args.size();
  res->args =
      static_cast<grpc_arg*>(gpr_malloc(sizeof(grpc_arg) * args.size()));
  for (int i = 0; i < args.size(); i++) {
    res->args[i].key = gpr_strdup(args[i].key().c_str());
    switch (args[i].value_case()) {
      case api_fuzzer::ChannelArg::kStr:
        res->args[i].type = GRPC_ARG_STRING;
        res->args[i].value.string = gpr_strdup(args[i].str().c_str());
        break;
      case api_fuzzer::ChannelArg::kI:
        res->args[i].type = GRPC_ARG_INTEGER;
        res->args[i].value.integer = args[i].i();
        break;
      case api_fuzzer::ChannelArg::kResourceQuota:
        res->args[i].type = GRPC_ARG_POINTER;
        res->args[i].value.pointer.p = g_resource_quota;
        res->args[i].value.pointer.vtable = grpc_resource_quota_arg_vtable();
        break;
    }
  }
  return res;
}

template <typename T>
grpc_slice ReadSlice(const T& proto_slice) {
  grpc_slice slice = grpc_slice_from_copied_buffer(proto_slice.value().data(),
                                                   proto_slice.value().size());
  if (proto_slice.intern()) {
    slice = grpc_slice_intern(slice);
  }
  return slice;
}

DEFINE_PROTO_FUZZER(const api_fuzzer::Msg& msg) {
  grpc_test_only_set_slice_hash_seed(0);
  char* grpc_trace_fuzzer = gpr_getenv("GRPC_TRACE_FUZZER");
  if (squelch && grpc_trace_fuzzer == nullptr) gpr_set_log_function(dont_log);
  gpr_free(grpc_trace_fuzzer);
  grpc_set_tcp_client_impl(&fuzz_tcp_client_vtable);
  gpr_now_impl = now_impl;
  grpc_init();
  grpc_timer_manager_set_threading(false);
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);
  }
  grpc_set_resolver_impl(&fuzzer_resolver);
  grpc_dns_lookup_ares_locked = my_dns_lookup_ares_locked;
  grpc_cancel_ares_request_locked = my_cancel_ares_request_locked;

  GPR_ASSERT(g_channel == nullptr);
  GPR_ASSERT(g_server == nullptr);

  bool server_shutdown = false;
  int pending_server_shutdowns = 0;
  int pending_channel_watches = 0;
  int pending_pings = 0;

  g_active_call = new_call(nullptr, ROOT);
  g_resource_quota = grpc_resource_quota_create("api_fuzzer");

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);

  int action_index = 0;
  auto no_more_actions = [&]() { action_index = msg.actions_size(); };

  while (action_index < msg.actions_size() || g_channel != nullptr ||
         g_server != nullptr || pending_channel_watches > 0 ||
         pending_pings > 0 || ActiveCall() != nullptr) {
    if (action_index == msg.actions_size()) {
      if (g_channel != nullptr) {
        grpc_channel_destroy(g_channel);
        g_channel = nullptr;
      }
      if (g_server != nullptr) {
        if (!server_shutdown) {
          grpc_server_shutdown_and_notify(
              g_server, cq,
              AssertSuccessAndDecrement(&pending_server_shutdowns));
          server_shutdown = true;
          pending_server_shutdowns++;
        } else if (pending_server_shutdowns == 0) {
          grpc_server_destroy(g_server);
          g_server = nullptr;
        }
      }
      call_state* s = g_active_call;
      do {
        if (s->type != PENDING_SERVER && s->call != nullptr) {
          s = destroy_call(s);
        } else {
          s = s->next;
        }
      } while (s != g_active_call);

      g_now = gpr_time_add(g_now, gpr_time_from_seconds(1, GPR_TIMESPAN));
      grpc_timer_manager_tick();
      continue;
    }

    grpc_timer_manager_tick();

    const api_fuzzer::Action& action = msg.actions(action_index);
    action_index++;
    switch (action.type_case()) {
      // tickle completion queue
      case api_fuzzer::Action::kPollCq: {
        grpc_event ev = grpc_completion_queue_next(
            cq, gpr_inf_past(GPR_CLOCK_REALTIME), nullptr);
        switch (ev.type) {
          case GRPC_OP_COMPLETE: {
            static_cast<Validator*>(ev.tag)->Run(ev.success);
            break;
          }
          case GRPC_QUEUE_TIMEOUT:
            break;
          case GRPC_QUEUE_SHUTDOWN:
            abort();
            break;
        }
        break;
      }
      // increment global time
      case api_fuzzer::Action::kAdvanceTime: {
        g_now = gpr_time_add(
            g_now, gpr_time_from_micros(action.advance_time(), GPR_TIMESPAN));
        break;
      }
      // create an insecure channel
      case api_fuzzer::Action::kCreateChannel: {
        if (g_channel == nullptr) {
          grpc_channel_args* args =
              ReadArgs(action.create_channel().channel_args());
          if (action.create_channel().has_channel_creds()) {
            grpc_channel_credentials* creds =
                ReadChannelCreds(action.create_channel().channel_creds());
            g_channel = grpc_secure_channel_create(
                creds, action.create_channel().target().c_str(), args, nullptr);
          } else {
            g_channel = grpc_insecure_channel_create(
                action.create_channel().target().c_str(), args, nullptr);
          }
          GPR_ASSERT(g_channel != nullptr);
          {
            grpc_core::ExecCtx exec_ctx;
            grpc_channel_args_destroy(args);
          }
        } else {
          no_more_actions();
        }
        break;
      }
      // destroy a channel
      case api_fuzzer::Action::kCloseChannel: {
        if (g_channel != nullptr) {
          grpc_channel_destroy(g_channel);
          g_channel = nullptr;
        } else {
          no_more_actions();
        }
        break;
      }
      // bring up a server
      case api_fuzzer::Action::kCreateServer: {
        if (g_server == nullptr) {
          grpc_channel_args* args =
              ReadArgs(action.create_server().channel_args());
          g_server = grpc_server_create(args, nullptr);
          GPR_ASSERT(g_server != nullptr);
          {
            grpc_core::ExecCtx exec_ctx;
            grpc_channel_args_destroy(args);
          }
          grpc_server_register_completion_queue(g_server, cq, nullptr);
          grpc_server_start(g_server);
          server_shutdown = false;
          GPR_ASSERT(pending_server_shutdowns == 0);
        } else {
          no_more_actions();
        }
        break;
      }
      // begin server shutdown
      case api_fuzzer::Action::kShutdownServer: {
        if (g_server != nullptr) {
          grpc_server_shutdown_and_notify(
              g_server, cq,
              AssertSuccessAndDecrement(&pending_server_shutdowns));
          pending_server_shutdowns++;
          server_shutdown = true;
        } else {
          no_more_actions();
        }
        break;
      }
      // cancel all calls if shutdown
      case api_fuzzer::Action::kCancelAllCallsIfShutdown: {
        if (g_server != nullptr && server_shutdown) {
          grpc_server_cancel_all_calls(g_server);
        } else {
          no_more_actions();
        }
        break;
      }
      // destroy server
      case api_fuzzer::Action::kDestroyServerIfReady: {
        if (g_server != nullptr && server_shutdown &&
            pending_server_shutdowns == 0) {
          grpc_server_destroy(g_server);
          g_server = nullptr;
        } else {
          no_more_actions();
        }
        break;
      }
      // check connectivity
      case api_fuzzer::Action::kCheckConnectivity: {
        if (g_channel != nullptr) {
          grpc_channel_check_connectivity_state(g_channel,
                                                action.check_connectivity());
        } else {
          no_more_actions();
        }
        break;
      }
      // watch connectivity
      case api_fuzzer::Action::kWatchConnectivity: {
        if (g_channel != nullptr) {
          grpc_connectivity_state st =
              grpc_channel_check_connectivity_state(g_channel, 0);
          if (st != GRPC_CHANNEL_SHUTDOWN) {
            gpr_timespec deadline =
                gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                             gpr_time_from_micros(action.watch_connectivity(),
                                                  GPR_TIMESPAN));
            grpc_channel_watch_connectivity_state(
                g_channel, st, deadline, cq,
                ValidateConnectivityWatch(deadline, &pending_channel_watches));
            pending_channel_watches++;
          }
        } else {
          no_more_actions();
        }
        break;
      }
      // create a call
      case api_fuzzer::Action::kCreateCall: {
        bool ok = true;
        if (g_channel == nullptr) ok = false;
        grpc_call* parent_call = ActiveCall();
        if (parent_call != nullptr && parent_call->type() == CLIENT) {
          parent_call = nullptr;
        }
        grpc_slice method = ReadSlice(action.create_call().method());
        if (GRPC_SLICE_LENGTH(method) == 0) {
          ok = false;
        }
        grpc_slice host = ReadSlice(action.create_call().host());
        gpr_timespec deadline = gpr_time_add(
            gpr_now(GPR_CLOCK_REALTIME),
            gpr_time_from_micros(action.create_call().timeout(), GPR_TIMESPAN));

        if (ok) {
          call_state* cs = new_call(g_active_call, CLIENT);
          cs->call = grpc_channel_create_call(
              g_channel, parent_call, action.create_call().propagation_mask(),
              cq, method, &host, deadline, nullptr);
        } else {
          end(&inp);
        }
        grpc_slice_unref(method);
        grpc_slice_unref(host);
        break;
      }
      // switch the 'current' call
      case 11: {
        g_active_call = g_active_call->next;
        break;
      }
      // queue some ops on a call
      case 12: {
        if (g_active_call->type == PENDING_SERVER ||
            g_active_call->type == ROOT || g_active_call->call == nullptr) {
          end(&inp);
          break;
        }
        size_t num_ops = grpc_fuzzer_get_next_byte(&inp);
        if (num_ops > 6) {
          end(&inp);
          break;
        }
        grpc_op* ops =
            static_cast<grpc_op*>(gpr_malloc(sizeof(grpc_op) * num_ops));
        if (num_ops > 0) memset(ops, 0, sizeof(grpc_op) * num_ops);
        bool ok = true;
        size_t i;
        grpc_op* op;
        uint8_t has_ops = 0;
        for (i = 0; i < num_ops; i++) {
          op = &ops[i];
          switch (grpc_fuzzer_get_next_byte(&inp)) {
            default:
              /* invalid value */
              op->op = (grpc_op_type)-1;
              ok = false;
              break;
            case GRPC_OP_SEND_INITIAL_METADATA:
              if (g_active_call->sent_initial_metadata) {
                ok = false;
              } else {
                g_active_call->sent_initial_metadata = true;
                op->op = GRPC_OP_SEND_INITIAL_METADATA;
                has_ops |= 1 << GRPC_OP_SEND_INITIAL_METADATA;
                read_metadata(&inp, &op->data.send_initial_metadata.count,
                              &op->data.send_initial_metadata.metadata,
                              g_active_call);
              }
              break;
            case GRPC_OP_SEND_MESSAGE:
              op->op = GRPC_OP_SEND_MESSAGE;
              if (g_active_call->send_message != nullptr) {
                ok = false;
              } else {
                has_ops |= 1 << GRPC_OP_SEND_MESSAGE;
                g_active_call->send_message =
                    op->data.send_message.send_message = read_message(&inp);
              }
              break;
            case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
              op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
              has_ops |= 1 << GRPC_OP_SEND_CLOSE_FROM_CLIENT;
              break;
            case GRPC_OP_SEND_STATUS_FROM_SERVER:
              op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
              has_ops |= 1 << GRPC_OP_SEND_STATUS_FROM_SERVER;
              read_metadata(
                  &inp,
                  &op->data.send_status_from_server.trailing_metadata_count,
                  &op->data.send_status_from_server.trailing_metadata,
                  g_active_call);
              op->data.send_status_from_server.status =
                  static_cast<grpc_status_code>(
                      grpc_fuzzer_get_next_byte(&inp));
              op->data.send_status_from_server.status_details =
                  add_slice_to_unref(g_active_call,
                                     read_buffer_like_slice(&inp));
              break;
            case GRPC_OP_RECV_INITIAL_METADATA:
              op->op = GRPC_OP_RECV_INITIAL_METADATA;
              has_ops |= 1 << GRPC_OP_RECV_INITIAL_METADATA;
              op->data.recv_initial_metadata.recv_initial_metadata =
                  &g_active_call->recv_initial_metadata;
              break;
            case GRPC_OP_RECV_MESSAGE:
              if (g_active_call->done_flags & DONE_FLAG_CALL_CLOSED) {
                ok = false;
              } else {
                op->op = GRPC_OP_RECV_MESSAGE;
                has_ops |= 1 << GRPC_OP_RECV_MESSAGE;
                op->data.recv_message.recv_message =
                    &g_active_call->recv_message;
              }
              break;
            case GRPC_OP_RECV_STATUS_ON_CLIENT:
              op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
              op->data.recv_status_on_client.status = &g_active_call->status;
              op->data.recv_status_on_client.trailing_metadata =
                  &g_active_call->recv_trailing_metadata;
              op->data.recv_status_on_client.status_details =
                  &g_active_call->recv_status_details;
              break;
            case GRPC_OP_RECV_CLOSE_ON_SERVER:
              op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
              has_ops |= 1 << GRPC_OP_RECV_CLOSE_ON_SERVER;
              op->data.recv_close_on_server.cancelled =
                  &g_active_call->cancelled;
              break;
          }
          op->reserved = nullptr;
          op->flags = grpc_fuzzer_get_next_uint32(&inp);
        }
        if (g_channel == nullptr) ok = false;
        if (ok) {
          validator* v = make_finished_batch_validator(g_active_call, has_ops);
          g_active_call->pending_ops++;
          grpc_call_error error = grpc_call_start_batch(
              g_active_call->call, ops, num_ops, v, nullptr);
          if (error != GRPC_CALL_OK) {
            v->validate(v->arg, false);
            gpr_free(v);
          }
        } else {
          end(&inp);
        }
        if (!ok && (has_ops & (1 << GRPC_OP_SEND_MESSAGE))) {
          grpc_byte_buffer_destroy(g_active_call->send_message);
          g_active_call->send_message = nullptr;
        }
        gpr_free(ops);

        break;
      }
      // cancel current call
      case 13: {
        if (g_active_call->type != ROOT && g_active_call->call != nullptr) {
          grpc_call_cancel(g_active_call->call, nullptr);
        } else {
          end(&inp);
        }
        break;
      }
      // get a calls peer
      case 14: {
        if (g_active_call->type != ROOT && g_active_call->call != nullptr) {
          free_non_null(grpc_call_get_peer(g_active_call->call));
        } else {
          end(&inp);
        }
        break;
      }
      // get a channels target
      case 15: {
        if (g_channel != nullptr) {
          free_non_null(grpc_channel_get_target(g_channel));
        } else {
          end(&inp);
        }
        break;
      }
      // send a ping on a channel
      case 16: {
        if (g_channel != nullptr) {
          pending_pings++;
          grpc_channel_ping(g_channel, cq,
                            create_validator(decrement, &pending_pings),
                            nullptr);
        } else {
          end(&inp);
        }
        break;
      }
      // enable a tracer
      case 17: {
        char* tracer = grpc_fuzzer_get_next_string(&inp, nullptr);
        grpc_tracer_set_enabled(tracer, 1);
        gpr_free(tracer);
        break;
      }
      // disable a tracer
      case 18: {
        char* tracer = grpc_fuzzer_get_next_string(&inp, nullptr);
        grpc_tracer_set_enabled(tracer, 0);
        gpr_free(tracer);
        break;
      }
      // request a server call
      case 19: {
        if (g_server == nullptr) {
          end(&inp);
          break;
        }
        call_state* cs = new_call(g_active_call, PENDING_SERVER);
        cs->pending_ops++;
        validator* v = create_validator(finished_request_call, cs);
        grpc_call_error error =
            grpc_server_request_call(g_server, &cs->call, &cs->call_details,
                                     &cs->recv_initial_metadata, cq, cq, v);
        if (error != GRPC_CALL_OK) {
          v->validate(v->arg, false);
          gpr_free(v);
        }
        break;
      }
      // destroy a call
      case 20: {
        if (g_active_call->type != ROOT &&
            g_active_call->type != PENDING_SERVER &&
            g_active_call->call != nullptr) {
          destroy_call(g_active_call);
        } else {
          end(&inp);
        }
        break;
      }
      // resize the buffer pool
      case 21: {
        grpc_resource_quota_resize(g_resource_quota, read_uint22(&inp));
        break;
      }
      // create a secure channel
      case 22: {
        if (g_channel == nullptr) {
          char* target = grpc_fuzzer_get_next_string(&inp, nullptr);
          char* target_uri;
          gpr_asprintf(&target_uri, "dns:%s", target);
          grpc_channel_args* args = read_args(&inp);
          grpc_channel_credentials* creds = read_channel_creds(&inp);
          g_channel =
              grpc_secure_channel_create(creds, target_uri, args, nullptr);
          GPR_ASSERT(g_channel != nullptr);
          {
            grpc_core::ExecCtx exec_ctx;
            grpc_channel_args_destroy(args);
          }
          gpr_free(target_uri);
          gpr_free(target);
          grpc_channel_credentials_release(creds);
        } else {
          end(&inp);
        }
        break;
      }
    }
  }

  GPR_ASSERT(g_channel == nullptr);
  GPR_ASSERT(g_server == nullptr);
  GPR_ASSERT(g_active_call->type == ROOT);
  GPR_ASSERT(g_active_call->next == g_active_call);
  gpr_free(g_active_call);

  grpc_completion_queue_shutdown(cq);
  GPR_ASSERT(
      grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME), nullptr)
          .type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cq);

  grpc_resource_quota_unref(g_resource_quota);

  grpc_shutdown_blocking();
  return 0;
}
