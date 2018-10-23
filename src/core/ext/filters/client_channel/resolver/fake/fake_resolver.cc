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

// This is similar to the sockaddr resolver, except that it supports a
// bunch of query args that are useful for dependency injection in tests.

#include <grpc/support/port_platform.h>

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"

namespace grpc_core {

// This cannot be in an anonymous namespace, because it is a friend of
// FakeResolverResponseGenerator.
class FakeResolver : public Resolver {
 public:
  explicit FakeResolver(const ResolverArgs& args);

  void NextLocked(grpc_channel_args** result,
                  grpc_closure* on_complete) override;

  void RequestReresolutionLocked() override;

 private:
  friend class FakeResolverResponseGenerator;

  virtual ~FakeResolver();

  void MaybeFinishNextLocked();

  void ShutdownLocked() override;

  // passed-in parameters
  grpc_channel_args* channel_args_ = nullptr;
  // If not NULL, the next set of resolution results to be returned to
  // NextLocked()'s closure.
  grpc_channel_args* next_results_ = nullptr;
  // Results to use for the pretended re-resolution in
  // RequestReresolutionLocked().
  grpc_channel_args* reresolution_results_ = nullptr;
  // pending next completion, or NULL
  grpc_closure* next_completion_ = nullptr;
  // target result address for next completion
  grpc_channel_args** target_result_ = nullptr;
  // if true, return failure
  bool return_failure_ = false;
};

FakeResolver::FakeResolver(const ResolverArgs& args) : Resolver(args.combiner) {
  channel_args_ = grpc_channel_args_copy(args.args);
  FakeResolverResponseGenerator* response_generator =
      FakeResolverResponseGenerator::GetFromArgs(args.args);
  if (response_generator != nullptr) response_generator->resolver_ = this;
}

FakeResolver::~FakeResolver() {
  grpc_channel_args_destroy(next_results_);
  grpc_channel_args_destroy(reresolution_results_);
  grpc_channel_args_destroy(channel_args_);
}

void FakeResolver::NextLocked(grpc_channel_args** target_result,
                              grpc_closure* on_complete) {
  GPR_ASSERT(next_completion_ == nullptr);
  next_completion_ = on_complete;
  target_result_ = target_result;
  MaybeFinishNextLocked();
}

void FakeResolver::RequestReresolutionLocked() {
  if (reresolution_results_ != nullptr) {
    grpc_channel_args_destroy(next_results_);
    next_results_ = grpc_channel_args_copy(reresolution_results_);
    MaybeFinishNextLocked();
  }
}

void FakeResolver::MaybeFinishNextLocked() {
  if (next_completion_ != nullptr &&
      (next_results_ != nullptr || return_failure_)) {
    *target_result_ =
        return_failure_ ? nullptr
                        : grpc_channel_args_union(next_results_, channel_args_);
    grpc_channel_args_destroy(next_results_);
    next_results_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_NONE);
    next_completion_ = nullptr;
    return_failure_ = false;
  }
}

void FakeResolver::ShutdownLocked() {
  if (next_completion_ != nullptr) {
    *target_result_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                             "Resolver Shutdown"));
    next_completion_ = nullptr;
  }
}

//
// FakeResolverResponseGenerator
//

struct SetResponseClosureArg {
  grpc_closure set_response_closure;
  FakeResolverResponseGenerator* generator;
  grpc_channel_args* response;
};

void FakeResolverResponseGenerator::SetResponseLocked(void* arg,
                                                      grpc_error* error) {
  SetResponseClosureArg* closure_arg = static_cast<SetResponseClosureArg*>(arg);
  FakeResolver* resolver = closure_arg->generator->resolver_;
  grpc_channel_args_destroy(resolver->next_results_);
  resolver->next_results_ = closure_arg->response;
  resolver->MaybeFinishNextLocked();
  Delete(closure_arg);
}

void FakeResolverResponseGenerator::SetResponse(grpc_channel_args* response) {
  GPR_ASSERT(response != nullptr);
  GPR_ASSERT(resolver_ != nullptr);
  SetResponseClosureArg* closure_arg = New<SetResponseClosureArg>();
  closure_arg->generator = this;
  closure_arg->response = grpc_channel_args_copy(response);
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_INIT(&closure_arg->set_response_closure, SetResponseLocked,
                        closure_arg,
                        grpc_combiner_scheduler(resolver_->combiner())),
      GRPC_ERROR_NONE);
}

void FakeResolverResponseGenerator::SetReresolutionResponseLocked(
    void* arg, grpc_error* error) {
  SetResponseClosureArg* closure_arg = static_cast<SetResponseClosureArg*>(arg);
  FakeResolver* resolver = closure_arg->generator->resolver_;
  grpc_channel_args_destroy(resolver->reresolution_results_);
  resolver->reresolution_results_ = closure_arg->response;
  Delete(closure_arg);
}

void FakeResolverResponseGenerator::SetReresolutionResponse(
    grpc_channel_args* response) {
  GPR_ASSERT(resolver_ != nullptr);
  SetResponseClosureArg* closure_arg = New<SetResponseClosureArg>();
  closure_arg->generator = this;
  closure_arg->response =
      response != nullptr ? grpc_channel_args_copy(response) : nullptr;
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_INIT(&closure_arg->set_response_closure,
                        SetReresolutionResponseLocked, closure_arg,
                        grpc_combiner_scheduler(resolver_->combiner())),
      GRPC_ERROR_NONE);
}

void FakeResolverResponseGenerator::SetFailureLocked(void* arg,
                                                     grpc_error* error) {
  SetResponseClosureArg* closure_arg = static_cast<SetResponseClosureArg*>(arg);
  FakeResolver* resolver = closure_arg->generator->resolver_;
  resolver->return_failure_ = true;
  resolver->MaybeFinishNextLocked();
  Delete(closure_arg);
}

void FakeResolverResponseGenerator::SetFailure() {
  GPR_ASSERT(resolver_ != nullptr);
  SetResponseClosureArg* closure_arg = New<SetResponseClosureArg>();
  closure_arg->generator = this;
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_INIT(&closure_arg->set_response_closure, SetFailureLocked,
                        closure_arg,
                        grpc_combiner_scheduler(resolver_->combiner())),
      GRPC_ERROR_NONE);
}

namespace {

static void* response_generator_arg_copy(void* p) {
  FakeResolverResponseGenerator* generator =
      static_cast<FakeResolverResponseGenerator*>(p);
  // TODO(roth): We currently deal with this ref manually.  Once the
  // new channel args code is converted to C++, find a way to track this ref
  // in a cleaner way.
  RefCountedPtr<FakeResolverResponseGenerator> copy = generator->Ref();
  copy.release();
  return p;
}

static void response_generator_arg_destroy(void* p) {
  FakeResolverResponseGenerator* generator =
      static_cast<FakeResolverResponseGenerator*>(p);
  generator->Unref();
}

static int response_generator_cmp(void* a, void* b) { return GPR_ICMP(a, b); }

static const grpc_arg_pointer_vtable response_generator_arg_vtable = {
    response_generator_arg_copy, response_generator_arg_destroy,
    response_generator_cmp};

}  // namespace

grpc_arg FakeResolverResponseGenerator::MakeChannelArg(
    FakeResolverResponseGenerator* generator) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = (char*)GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR;
  arg.value.pointer.p = generator;
  arg.value.pointer.vtable = &response_generator_arg_vtable;
  return arg;
}

FakeResolverResponseGenerator* FakeResolverResponseGenerator::GetFromArgs(
    const grpc_channel_args* args) {
  const grpc_arg* arg =
      grpc_channel_args_find(args, GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) return nullptr;
  return static_cast<FakeResolverResponseGenerator*>(arg->value.pointer.p);
}

//
// Factory
//

namespace {

class FakeResolverFactory : public ResolverFactory {
 public:
  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    return OrphanablePtr<Resolver>(New<FakeResolver>(args));
  }

  const char* scheme() const override { return "fake"; }
};

}  // namespace

}  // namespace grpc_core

void grpc_resolver_fake_init() {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::UniquePtr<grpc_core::ResolverFactory>(
          grpc_core::New<grpc_core::FakeResolverFactory>()));
}

void grpc_resolver_fake_shutdown() {}
