//
//
// Copyright 2015 gRPC authors.
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
//

#include "src/core/lib/channel/channelz.h"

#include <string.h>

#include <functional>
#include <memory>
#include <string>

#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

static std::unique_ptr<CoreTestFixture> begin_test(
    const CoreTestConfiguration& config, const char* test_name,
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  auto f = config.create_fixture(grpc_core::ChannelArgs::FromC(client_args),
                                 grpc_core::ChannelArgs::FromC(server_args));
  f->InitServer(grpc_core::ChannelArgs::FromC(server_args));
  f->InitClient(grpc_core::ChannelArgs::FromC(client_args));
  return f;
}

static void run_one_request(const CoreTestConfiguration& /*config*/,
                            CoreTestFixture* f, bool request_is_success) {
  grpc_call* c;
  grpc_call* s;
  grpc_core::CqVerifier cqv(f->cq());
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  c = grpc_channel_create_call(f->client(), nullptr, GRPC_PROPAGATE_DEFAULTS,
                               f->cq(), grpc_slice_from_static_string("/foo"),
                               nullptr, deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.error_string = nullptr;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error = grpc_server_request_call(f->server(), &s, &call_details,
                                   &request_metadata_recv, f->cq(), f->cq(),
                                   grpc_core::CqVerifier::tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(grpc_core::CqVerifier::tag(101), true);
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status =
      request_is_success ? GRPC_STATUS_OK : GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(grpc_core::CqVerifier::tag(102), true);
  cqv.Expect(grpc_core::CqVerifier::tag(1), true);
  cqv.Verify();

  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);
}

static void test_channelz(const CoreTestConfiguration& config) {
  grpc_arg arg[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE),
          0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ENABLE_CHANNELZ), true)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};

  auto f = begin_test(config, "test_channelz", &args, &args);
  grpc_core::channelz::ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(f->client());
  GPR_ASSERT(channelz_channel != nullptr);

  grpc_core::channelz::ServerNode* channelz_server =
      grpc_core::Server::FromC(f->server())->channelz_node();
  GPR_ASSERT(channelz_server != nullptr);

  std::string json = channelz_channel->RenderJsonString();
  // nothing is present yet
  GPR_ASSERT(json.find("\"callsStarted\"") == json.npos);
  GPR_ASSERT(json.find("\"callsFailed\"") == json.npos);
  GPR_ASSERT(json.find("\"callsSucceeded\"") == json.npos);

  // one successful request
  run_one_request(config, f.get(), true);

  json = channelz_channel->RenderJsonString();
  GPR_ASSERT(json.find("\"callsStarted\":\"1\"") != json.npos);
  GPR_ASSERT(json.find("\"callsSucceeded\":\"1\"") != json.npos);

  // one failed request
  run_one_request(config, f.get(), false);

  json = channelz_channel->RenderJsonString();
  GPR_ASSERT(json.find("\"callsStarted\":\"2\"") != json.npos);
  GPR_ASSERT(json.find("\"callsFailed\":\"1\"") != json.npos);
  GPR_ASSERT(json.find("\"callsSucceeded\":\"1\"") != json.npos);
  // channel tracing is not enabled, so these should not be preset.
  GPR_ASSERT(json.find("\"trace\"") == json.npos);
  GPR_ASSERT(json.find("\"description\":\"Channel created\"") == json.npos);
  GPR_ASSERT(json.find("\"severity\":\"CT_INFO\"") == json.npos);

  json = channelz_server->RenderJsonString();
  GPR_ASSERT(json.find("\"callsStarted\":\"2\"") != json.npos);
  GPR_ASSERT(json.find("\"callsFailed\":\"1\"") != json.npos);
  GPR_ASSERT(json.find("\"callsSucceeded\":\"1\"") != json.npos);
  // channel tracing is not enabled, so these should not be preset.
  GPR_ASSERT(json.find("\"trace\"") == json.npos);
  GPR_ASSERT(json.find("\"description\":\"Channel created\"") == json.npos);
  GPR_ASSERT(json.find("\"severity\":\"CT_INFO\"") == json.npos);

  json = channelz_server->RenderServerSockets(0, 100);
  GPR_ASSERT(json.find("\"end\":true") != json.npos);
}

static void test_channelz_with_channel_trace(
    const CoreTestConfiguration& config) {
  grpc_arg arg[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE),
          1024 * 1024),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ENABLE_CHANNELZ), true)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};

  auto f = begin_test(config, "test_channelz_with_channel_trace", &args, &args);
  grpc_core::channelz::ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(f->client());
  GPR_ASSERT(channelz_channel != nullptr);

  grpc_core::channelz::ServerNode* channelz_server =
      grpc_core::Server::FromC(f->server())->channelz_node();
  GPR_ASSERT(channelz_server != nullptr);

  run_one_request(config, f.get(), true);

  std::string json = channelz_channel->RenderJsonString();
  GPR_ASSERT(json.find("\"trace\"") != json.npos);
  GPR_ASSERT(json.find("\"description\":\"Channel created\"") != json.npos);
  GPR_ASSERT(json.find("\"severity\":\"CT_INFO\"") != json.npos);

  json = channelz_server->RenderJsonString();
  GPR_ASSERT(json.find("\"trace\"") != json.npos);
  GPR_ASSERT(json.find("\"description\":\"Server created\"") != json.npos);
  GPR_ASSERT(json.find("\"severity\":\"CT_INFO\"") != json.npos);
}

static void test_channelz_disabled(const CoreTestConfiguration& config) {
  grpc_arg arg[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE),
          0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ENABLE_CHANNELZ), false)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};

  auto f = begin_test(config, "test_channelz_disabled", &args, &args);
  grpc_core::channelz::ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(f->client());
  GPR_ASSERT(channelz_channel == nullptr);
  // one successful request
  run_one_request(config, f.get(), true);
  GPR_ASSERT(channelz_channel == nullptr);
}

void channelz(const CoreTestConfiguration& config) {
  test_channelz(config);
  test_channelz_with_channel_trace(config);
  test_channelz_disabled(config);
}

void channelz_pre_init(void) {}
