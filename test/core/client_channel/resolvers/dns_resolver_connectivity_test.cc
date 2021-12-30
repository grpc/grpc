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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolve_address_impl.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resolver/server_address.h"
#include "test/core/util/test_config.h"

static gpr_mu g_mu;
static bool g_fail_resolution = true;
static std::shared_ptr<grpc_core::WorkSerializer>* g_work_serializer;

namespace {

class TestDNSResolver : public grpc_core::DNSResolver {
 public:
  class TestDNSRequest : public grpc_core::DNSResolver::Request {
   public:
    explicit TestDNSRequest(
        std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
            on_done)
        : on_done_(std::move(on_done)) {}

    void Start() override {
      gpr_mu_lock(&g_mu);
      if (g_fail_resolution) {
        g_fail_resolution = false;
        gpr_mu_unlock(&g_mu);
        new grpc_core::DNSCallbackExecCtxScheduler(
            std::move(on_done_), absl::UnknownError("Forced Failure"));
      } else {
        gpr_mu_unlock(&g_mu);
        std::vector<grpc_resolved_address> addrs;
        grpc_resolved_address phony_resolved_address;
        memset(&phony_resolved_address, 0, sizeof(phony_resolved_address));
        addrs.push_back(phony_resolved_address);
        new grpc_core::DNSCallbackExecCtxScheduler(std::move(on_done_), addrs);
      }
    }

    void Orphan() override { Unref(); }

   private:
    std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_done_;
  };

  grpc_core::OrphanablePtr<grpc_core::DNSResolver::Request> ResolveName(
      absl::string_view name, absl::string_view /* default_port */,
      grpc_pollset_set* /* interested_parties */,
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done) override {
    GPR_ASSERT("test" == name);
    return grpc_core::MakeOrphanable<TestDNSRequest>(std::move(on_done));
  }

  absl::StatusOr<std::vector<grpc_resolved_address>> ResolveNameBlocking(
      absl::string_view /* name */,
      absl::string_view /* default_port */) override {
    GPR_ASSERT(0);
  }
};

}  // namespace

static grpc_ares_request* my_dns_lookup_ares(
    const char* /*dns_server*/, const char* addr, const char* /*default_port*/,
    grpc_pollset_set* /*interested_parties*/, grpc_closure* on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addresses,
    std::unique_ptr<grpc_core::ServerAddressList>* /*balancer_addresses*/,
    char** /*service_config_json*/, int /*query_timeout_ms*/) {  // NOLINT
  gpr_mu_lock(&g_mu);
  GPR_ASSERT(0 == strcmp("test", addr));
  grpc_error_handle error = GRPC_ERROR_NONE;
  if (g_fail_resolution) {
    g_fail_resolution = false;
    gpr_mu_unlock(&g_mu);
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Forced Failure");
  } else {
    gpr_mu_unlock(&g_mu);
    *addresses = absl::make_unique<grpc_core::ServerAddressList>();
    grpc_resolved_address phony_resolved_address;
    memset(&phony_resolved_address, 0, sizeof(phony_resolved_address));
    phony_resolved_address.len = 123;
    (*addresses)->emplace_back(phony_resolved_address, nullptr);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, error);
  return nullptr;
}

static void my_cancel_ares_request(grpc_ares_request* request) {
  GPR_ASSERT(request == nullptr);
}

static grpc_core::OrphanablePtr<grpc_core::Resolver> create_resolver(
    const char* name,
    std::unique_ptr<grpc_core::Resolver::ResultHandler> result_handler) {
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory("dns");
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(name);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "%s", uri.status().ToString().c_str());
    GPR_ASSERT(uri.ok());
  }
  grpc_core::ResolverArgs args;
  args.uri = std::move(*uri);
  args.work_serializer = *g_work_serializer;
  args.result_handler = std::move(result_handler);
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(std::move(args));
  return resolver;
}

class ResultHandler : public grpc_core::Resolver::ResultHandler {
 public:
  struct ResolverOutput {
    grpc_core::Resolver::Result result;
    gpr_event ev;

    ResolverOutput() { gpr_event_init(&ev); }
  };

  void SetOutput(ResolverOutput* output) {
    gpr_atm_rel_store(&output_, reinterpret_cast<gpr_atm>(output));
  }

  void ReportResult(grpc_core::Resolver::Result result) override {
    ResolverOutput* output =
        reinterpret_cast<ResolverOutput*>(gpr_atm_acq_load(&output_));
    GPR_ASSERT(output != nullptr);
    output->result = std::move(result);
    gpr_event_set(&output->ev, reinterpret_cast<void*>(1));
  }

 private:
  gpr_atm output_ = 0;  // ResolverOutput*
};

// interleave waiting for an event with a timer check
static bool wait_loop(int deadline_seconds, gpr_event* ev) {
  while (deadline_seconds) {
    gpr_log(GPR_DEBUG, "Test: waiting for %d more seconds", deadline_seconds);
    if (gpr_event_wait(ev, grpc_timeout_seconds_to_deadline(1))) return true;
    deadline_seconds--;

    grpc_core::ExecCtx exec_ctx;
    grpc_timer_check(nullptr);
  }
  return false;
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);

  grpc_init();
  gpr_mu_init(&g_mu);
  auto work_serializer = std::make_shared<grpc_core::WorkSerializer>();
  g_work_serializer = &work_serializer;
  grpc_core::SetDNSResolver(new TestDNSResolver());
  grpc_dns_lookup_ares = my_dns_lookup_ares;
  grpc_cancel_ares_request = my_cancel_ares_request;

  {
    grpc_core::ExecCtx exec_ctx;
    ResultHandler* result_handler = new ResultHandler();
    grpc_core::OrphanablePtr<grpc_core::Resolver> resolver = create_resolver(
        "dns:test",
        std::unique_ptr<grpc_core::Resolver::ResultHandler>(result_handler));
    ResultHandler::ResolverOutput output1;
    result_handler->SetOutput(&output1);
    resolver->StartLocked();
    grpc_core::ExecCtx::Get()->Flush();
    GPR_ASSERT(wait_loop(5, &output1.ev));
    GPR_ASSERT(!output1.result.addresses.ok());

    ResultHandler::ResolverOutput output2;
    result_handler->SetOutput(&output2);
    grpc_core::ExecCtx::Get()->Flush();
    GPR_ASSERT(wait_loop(30, &output2.ev));
    GPR_ASSERT(output2.result.addresses.ok());
    GPR_ASSERT(!output2.result.addresses->empty());
  }

  grpc_shutdown();
  gpr_mu_destroy(&g_mu);
}
