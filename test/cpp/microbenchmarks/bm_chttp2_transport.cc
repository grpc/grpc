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

/* Microbenchmarks around CHTTP2 transport operations */

#include <benchmark/benchmark.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "test/core/util/resource_user_util.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/bm_transport.h"
#include "test/cpp/util/test_config.h"

////////////////////////////////////////////////////////////////////////////////
// Helper classes
//

class PhonyEndpoint : public grpc_endpoint {
 public:
  PhonyEndpoint() {
    static const grpc_endpoint_vtable my_vtable = {read,
                                                   write,
                                                   add_to_pollset,
                                                   add_to_pollset_set,
                                                   delete_from_pollset_set,
                                                   shutdown,
                                                   destroy,
                                                   get_peer,
                                                   get_local_address,
                                                   get_fd,
                                                   can_track_err};
    grpc_endpoint::vtable = &my_vtable;
  }

  void PushInput(grpc_slice slice) {
    if (read_cb_ == nullptr) {
      GPR_ASSERT(!have_slice_);
      buffered_slice_ = slice;
      have_slice_ = true;
      return;
    }
    grpc_slice_buffer_add(slices_, slice);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, read_cb_, GRPC_ERROR_NONE);
    read_cb_ = nullptr;
  }

 private:
  grpc_closure* read_cb_ = nullptr;
  grpc_slice_buffer* slices_ = nullptr;
  bool have_slice_ = false;
  grpc_slice buffered_slice_;

  void QueueRead(grpc_slice_buffer* slices, grpc_closure* cb) {
    GPR_ASSERT(read_cb_ == nullptr);
    if (have_slice_) {
      have_slice_ = false;
      grpc_slice_buffer_add(slices, buffered_slice_);
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
      return;
    }
    read_cb_ = cb;
    slices_ = slices;
  }

  static void read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, bool /*urgent*/) {
    static_cast<PhonyEndpoint*>(ep)->QueueRead(slices, cb);
  }

  static void write(grpc_endpoint* /*ep*/, grpc_slice_buffer* /*slices*/,
                    grpc_closure* cb, void* /*arg*/) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
  }

  static void add_to_pollset(grpc_endpoint* /*ep*/, grpc_pollset* /*pollset*/) {
  }

  static void add_to_pollset_set(grpc_endpoint* /*ep*/,
                                 grpc_pollset_set* /*pollset*/) {}

  static void delete_from_pollset_set(grpc_endpoint* /*ep*/,
                                      grpc_pollset_set* /*pollset*/) {}

  static void shutdown(grpc_endpoint* ep, grpc_error_handle why) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                            static_cast<PhonyEndpoint*>(ep)->read_cb_, why);
  }

  static void destroy(grpc_endpoint* ep) {
    delete static_cast<PhonyEndpoint*>(ep);
  }

  static absl::string_view get_peer(grpc_endpoint* /*ep*/) { return "test"; }
  static absl::string_view get_local_address(grpc_endpoint* /*ep*/) {
    return "test";
  }
  static int get_fd(grpc_endpoint* /*ep*/) { return 0; }
  static bool can_track_err(grpc_endpoint* /*ep*/) { return false; }
};

class Chttp2Fixture {
 public:
  Chttp2Fixture(const grpc::ChannelArguments& args, bool client) {
    grpc_channel_args c_args = args.c_channel_args();
    ep_ = new PhonyEndpoint;
    t_ = grpc_create_chttp2_transport(&c_args, ep_, client,
                                      grpc_resource_user_create_unlimited());
    grpc_chttp2_transport_start_reading(t_, nullptr, nullptr, nullptr);
    FlushExecCtx();
  }

  void FlushExecCtx() { grpc_core::ExecCtx::Get()->Flush(); }

  ~Chttp2Fixture() { grpc_transport_destroy(t_); }

  grpc_chttp2_transport* chttp2_transport() {
    return reinterpret_cast<grpc_chttp2_transport*>(t_);
  }
  grpc_transport* transport() { return t_; }

  void PushInput(grpc_slice slice) { ep_->PushInput(slice); }
  void PushInitialMetadata(grpc_slice slice) { ep_->PushInput(slice); }

  static grpc_slice RepresentativeServerInitialMetadata() {
    return SLICE_FROM_BUFFER(
        "\x00\x00\x00\x04\x00\x00\x00\x00\x00"
        // Generated using:
        // tools/codegen/core/gen_header_frame.py <
        // test/cpp/microbenchmarks/representative_server_initial_metadata.headers
        "\x00\x00X\x01\x04\x00\x00\x00\x01"
        "\x10\x07:status\x03"
        "200"
        "\x10\x0c"
        "content-type\x10"
        "application/grpc"
        "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip");
  }

 private:
  PhonyEndpoint* ep_;
  grpc_transport* t_;
};

std::vector<std::unique_ptr<gpr_event>> done_events;

BENCHMARK_TEMPLATE(BM_StreamCreateDestroy, Chttp2Fixture);
BENCHMARK_TEMPLATE(BM_StreamCreateSendInitialMetadataDestroy, Chttp2Fixture,
                   RepresentativeClientInitialMetadata);
BENCHMARK_TEMPLATE(BM_TransportEmptyOp, Chttp2Fixture);
BENCHMARK_TEMPLATE(BM_TransportStreamSend, Chttp2Fixture)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_TransportStreamRecv, Chttp2Fixture)
    ->Range(0, 128 * 1024 * 1024);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
