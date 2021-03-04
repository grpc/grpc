/*
 *
 * Copyright 2018 gRPC authors.
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

#include "test/core/end2end/end2end_tests.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

typedef struct inproc_fixture_data {
  bool phony;  // reserved for future expansion. Struct can't be empty
} inproc_fixture_data;

namespace {
template <typename F>
class CQDeletingCallback : public grpc_experimental_completion_queue_functor {
 public:
  explicit CQDeletingCallback(F f) : func_(f) {
    functor_run = &CQDeletingCallback::Run;
    inlineable = false;
  }
  ~CQDeletingCallback() {}
  static void Run(grpc_experimental_completion_queue_functor* cb, int ok) {
    auto* callback = static_cast<CQDeletingCallback*>(cb);
    callback->func_(static_cast<bool>(ok));
    delete callback;
  }

 private:
  F func_;
};

template <typename F>
grpc_experimental_completion_queue_functor* NewDeletingCallback(F f) {
  return new CQDeletingCallback<F>(f);
}

class ShutdownCallback : public grpc_experimental_completion_queue_functor {
 public:
  ShutdownCallback() : done_(false) {
    functor_run = &ShutdownCallback::StaticRun;
    inlineable = false;
    gpr_mu_init(&mu_);
    gpr_cv_init(&cv_);
  }
  ~ShutdownCallback() {
    gpr_mu_destroy(&mu_);
    gpr_cv_destroy(&cv_);
  }
  static void StaticRun(grpc_experimental_completion_queue_functor* cb,
                        int ok) {
    auto* callback = static_cast<ShutdownCallback*>(cb);
    callback->Run(static_cast<bool>(ok));
  }
  void Run(bool /*ok*/) {
    gpr_log(GPR_DEBUG, "CQ shutdown notification invoked");
    gpr_mu_lock(&mu_);
    done_ = true;
    gpr_cv_broadcast(&cv_);
    gpr_mu_unlock(&mu_);
  }
  // The Wait function waits for a specified amount of
  // time for the completion of the shutdown and returns
  // whether it was successfully shut down
  bool Wait(gpr_timespec deadline) {
    gpr_mu_lock(&mu_);
    while (!done_ && !gpr_cv_wait(&cv_, &mu_, deadline)) {
    }
    bool ret = done_;
    gpr_mu_unlock(&mu_);
    return ret;
  }

 private:
  bool done_;
  gpr_mu mu_;
  gpr_cv cv_;
};

ShutdownCallback* g_shutdown_callback;
}  // namespace

// The following global structure is the tag collection. It holds
// all information related to tags expected and tags received
// during the execution, with each callback setting a tag.
// The tag sets are implemented and checked using arrays and
// linear lookups (rather than maps) so that this test doesn't
// need the C++ standard library.
static gpr_mu tags_mu;
static gpr_cv tags_cv;
const size_t kAvailableTags = 4;
bool tags[kAvailableTags];
bool tags_valid[kAvailableTags];
bool tags_expected[kAvailableTags];
bool tags_needed[kAvailableTags];

// Mark that a tag is expected; this function must be executed in the
// main thread only while there are no other threads altering the
// expectation set (e.g., by calling expect_tag or verify_tags)
static void expect_tag(intptr_t tag, bool ok) {
  size_t idx = static_cast<size_t>(tag);
  GPR_ASSERT(idx < kAvailableTags);
  tags_needed[idx] = true;
  tags_expected[idx] = ok;
}

// Check that the expected tags have reached, within a certain
// deadline. This must also be executed only on the main thread while
// there are no other threads altering the expectation set (e.g., by
// calling expect_tag or verify_tags). The tag verifier doesn't have
// to drive the CQ at all (unlike the next-based end2end tests)
// because the tags will get set when the callbacks are executed,
// which happens when a particular batch related to a callback is
// complete.
static void verify_tags(gpr_timespec deadline) {
  bool done = false;

  gpr_mu_lock(&tags_mu);
  while (!done) {
    done = gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) > 0;
    for (size_t i = 0; i < kAvailableTags; i++) {
      if (tags_needed[i]) {
        if (tags_valid[i]) {
          gpr_log(GPR_DEBUG, "Verifying tag %d", static_cast<int>(i));
          if (tags[i] != tags_expected[i]) {
            gpr_log(GPR_ERROR, "Got wrong result (%d instead of %d) for tag %d",
                    tags[i], tags_expected[i], static_cast<int>(i));
            GPR_ASSERT(false);
          }
          tags_valid[i] = false;
          tags_needed[i] = false;
        } else if (done) {
          gpr_log(GPR_ERROR, "Didn't get tag %d", static_cast<int>(i));
          GPR_ASSERT(false);
        }
      }
    }
    bool empty = true;
    for (size_t i = 0; i < kAvailableTags; i++) {
      if (tags_needed[i]) {
        empty = false;
      }
    }
    done = done || empty;
    if (done) {
      for (size_t i = 0; i < kAvailableTags; i++) {
        if (tags_valid[i]) {
          gpr_log(GPR_ERROR, "Got unexpected tag %d and result %d",
                  static_cast<int>(i), tags[i]);
          GPR_ASSERT(false);
        }
        tags_valid[i] = false;
      }
    } else {
      gpr_cv_wait(&tags_cv, &tags_mu, deadline);
    }
  }
  gpr_mu_unlock(&tags_mu);
}

// This function creates a callback functor that emits the
// desired tag into the global tag set
static grpc_experimental_completion_queue_functor* tag(intptr_t t) {
  auto func = [t](bool ok) {
    gpr_mu_lock(&tags_mu);
    gpr_log(GPR_DEBUG, "Completing operation %" PRIdPTR, t);
    bool was_empty = true;
    for (size_t i = 0; i < kAvailableTags; i++) {
      if (tags_valid[i]) {
        was_empty = false;
      }
    }
    size_t idx = static_cast<size_t>(t);
    tags[idx] = ok;
    tags_valid[idx] = true;
    if (was_empty) {
      gpr_cv_signal(&tags_cv);
    }
    gpr_mu_unlock(&tags_mu);
  };
  auto cb = NewDeletingCallback(func);
  return cb;
}

static grpc_end2end_test_fixture inproc_create_fixture(
    grpc_channel_args* /*client_args*/, grpc_channel_args* /*server_args*/) {
  grpc_end2end_test_fixture f;
  inproc_fixture_data* ffd = static_cast<inproc_fixture_data*>(
      gpr_malloc(sizeof(inproc_fixture_data)));
  memset(&f, 0, sizeof(f));

  f.fixture_data = ffd;
  g_shutdown_callback = new ShutdownCallback();
  f.cq =
      grpc_completion_queue_create_for_callback(g_shutdown_callback, nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);

  return f;
}

void inproc_init_client(grpc_end2end_test_fixture* f,
                        grpc_channel_args* client_args) {
  f->client = grpc_inproc_channel_create(f->server, client_args, nullptr);
  GPR_ASSERT(f->client);
}

void inproc_init_server(grpc_end2end_test_fixture* f,
                        grpc_channel_args* server_args) {
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_start(f->server);
}

void inproc_tear_down(grpc_end2end_test_fixture* f) {
  inproc_fixture_data* ffd = static_cast<inproc_fixture_data*>(f->fixture_data);
  gpr_free(ffd);
}

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            grpc_channel_args* client_args,
                                            grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_from_now(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_from_now() { return n_seconds_from_now(5); }

static void drain_cq(grpc_completion_queue* /*cq*/) {
  // Wait for the shutdown callback to arrive, or fail the test
  GPR_ASSERT(g_shutdown_callback->Wait(five_seconds_from_now()));
  gpr_log(GPR_DEBUG, "CQ shutdown wait complete");
  delete g_shutdown_callback;
}

static void shutdown_server(grpc_end2end_test_fixture* f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(
      f->server, f->shutdown_cq,
      reinterpret_cast<void*>(static_cast<intptr_t>(1000)));
  GPR_ASSERT(
      grpc_completion_queue_pluck(f->shutdown_cq, (void*)((intptr_t)1000),
                                  grpc_timeout_seconds_to_deadline(5), nullptr)
          .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->server);
  f->server = nullptr;
}

static void shutdown_client(grpc_end2end_test_fixture* f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = nullptr;
}

static void end_test(grpc_end2end_test_fixture* f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
  grpc_completion_queue_destroy(f->shutdown_cq);
}

static void simple_request_body(grpc_end2end_test_config config,
                                grpc_end2end_test_fixture f) {
  grpc_call* c;
  grpc_call* s;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  const char* error_string;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char* peer;
  gpr_timespec deadline = five_seconds_from_now();

  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != nullptr);
  gpr_log(GPR_DEBUG, "client_peer_before_call=%s", peer);
  gpr_free(peer);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  // Create a basic client unary request batch (no payload)
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
  op->data.recv_status_on_client.error_string = &error_string;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Register a call at the server-side to match the incoming client call
  error = grpc_server_request_call(f.server, &s, &call_details,
                                   &request_metadata_recv, f.cq, f.cq, tag(2));
  GPR_ASSERT(GRPC_CALL_OK == error);

  // We expect that the server call creation callback (and no others) will
  // execute now since no other batch should be complete.
  expect_tag(2, true);
  verify_tags(deadline);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != nullptr);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != nullptr);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  // Create the server response batch (no payload)
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
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
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(3),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Both the client request and server response batches should get complete
  // now and we should see that their callbacks have been executed
  expect_tag(3, true);
  expect_tag(1, true);
  verify_tags(deadline);

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  // the following sanity check makes sure that the requested error string is
  // correctly populated by the core. It looks for certain substrings that are
  // not likely to change much. Some parts of the error, like time created,
  // obviously are not checked.
  GPR_ASSERT(nullptr != strstr(error_string, "xyz"));
  GPR_ASSERT(nullptr != strstr(error_string, "description"));
  GPR_ASSERT(nullptr != strstr(error_string, "Error received from peer"));
  GPR_ASSERT(nullptr != strstr(error_string, "grpc_message"));
  GPR_ASSERT(nullptr != strstr(error_string, "grpc_status"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 0);

  grpc_slice_unref(details);
  gpr_free(static_cast<void*>(const_cast<char*>(error_string)));
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  int expected_calls = 1;
  if (config.feature_mask & FEATURE_MASK_SUPPORTS_REQUEST_PROXYING) {
    expected_calls *= 2;
  }
}

static void test_invoke_simple_request(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;

  f = begin_test(config, "test_invoke_simple_request", nullptr, nullptr);
  simple_request_body(config, f);
  end_test(&f);
  config.tear_down_data(&f);
}

static void test_invoke_10_simple_requests(grpc_end2end_test_config config) {
  int i;
  grpc_end2end_test_fixture f =
      begin_test(config, "test_invoke_10_simple_requests", nullptr, nullptr);
  for (i = 0; i < 10; i++) {
    simple_request_body(config, f);
    gpr_log(GPR_INFO, "Running test: Passed simple request %d", i);
  }
  end_test(&f);
  config.tear_down_data(&f);
}

static void test_invoke_many_simple_requests(grpc_end2end_test_config config) {
  int i;
  const int many = 1000;
  grpc_end2end_test_fixture f =
      begin_test(config, "test_invoke_many_simple_requests", nullptr, nullptr);
  gpr_timespec t1 = gpr_now(GPR_CLOCK_MONOTONIC);
  for (i = 0; i < many; i++) {
    simple_request_body(config, f);
  }
  double us =
      gpr_timespec_to_micros(gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), t1)) /
      many;
  gpr_log(GPR_INFO, "Time per ping %f us", us);
  end_test(&f);
  config.tear_down_data(&f);
}

static void simple_request(grpc_end2end_test_config config) {
  int i;
  for (i = 0; i < 10; i++) {
    test_invoke_simple_request(config);
  }
  test_invoke_10_simple_requests(config);
  test_invoke_many_simple_requests(config);
}

static void simple_request_pre_init() {
  gpr_mu_init(&tags_mu);
  gpr_cv_init(&tags_cv);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"inproc-callback", FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER, nullptr,
     inproc_create_fixture, inproc_init_client, inproc_init_server,
     inproc_tear_down},
};

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  simple_request_pre_init();
  simple_request(configs[0]);

  grpc_shutdown();

  return 0;
}
