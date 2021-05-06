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

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"

namespace grpc_core {

// This cannot be in an anonymous namespace, because it is a friend of
// FakeResolverResponseGenerator.
class FakeResolver : public Resolver {
 public:
  explicit FakeResolver(ResolverArgs args);

  void StartLocked() override;

  void RequestReresolutionLocked() override;

 private:
  friend class FakeResolverResponseGenerator;
  friend class FakeResolverResponseSetter;

  ~FakeResolver() override;

  void ShutdownLocked() override;

  void MaybeSendResultLocked();

  void ReturnReresolutionResult();

  // passed-in parameters
  grpc_channel_args* channel_args_ = nullptr;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
  RefCountedPtr<FakeResolverResponseGenerator> response_generator_;
  // If has_next_result_ is true, next_result_ is the next resolution result
  // to be returned.
  bool has_next_result_ = false;
  Result next_result_;
  // Result to use for the pretended re-resolution in
  // RequestReresolutionLocked().
  bool has_reresolution_result_ = false;
  Result reresolution_result_;
  // True after the call to StartLocked().
  bool started_ = false;
  // True after the call to ShutdownLocked().
  bool shutdown_ = false;
  // if true, return failure
  bool return_failure_ = false;
  // pending re-resolution
  bool reresolution_closure_pending_ = false;
};

FakeResolver::FakeResolver(ResolverArgs args)
    : work_serializer_(std::move(args.work_serializer)),
      result_handler_(std::move(args.result_handler)),
      response_generator_(
          FakeResolverResponseGenerator::GetFromArgs(args.args)) {
  // Channels sharing the same subchannels may have different resolver response
  // generators. If we don't remove this arg, subchannel pool will create new
  // subchannels for the same address instead of reusing existing ones because
  // of different values of this channel arg.
  const char* args_to_remove[] = {GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR};
  channel_args_ = grpc_channel_args_copy_and_remove(
      args.args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove));
  if (response_generator_ != nullptr) {
    response_generator_->SetFakeResolver(Ref());
  }
}

FakeResolver::~FakeResolver() { grpc_channel_args_destroy(channel_args_); }

void FakeResolver::StartLocked() {
  started_ = true;
  MaybeSendResultLocked();
}

void FakeResolver::RequestReresolutionLocked() {
  if (has_reresolution_result_ || return_failure_) {
    next_result_ = reresolution_result_;
    has_next_result_ = true;
    // Return the result in a different closure, so that we don't call
    // back into the LB policy while it's still processing the previous
    // update.
    if (!reresolution_closure_pending_) {
      reresolution_closure_pending_ = true;
      Ref().release();  // ref held by closure
      work_serializer_->Run([this]() { ReturnReresolutionResult(); },
                            DEBUG_LOCATION);
    }
  }
}

void FakeResolver::ShutdownLocked() {
  shutdown_ = true;
  if (response_generator_ != nullptr) {
    response_generator_->SetFakeResolver(nullptr);
    response_generator_.reset();
  }
}

void FakeResolver::MaybeSendResultLocked() {
  if (!started_ || shutdown_) return;
  if (return_failure_) {
    // TODO(roth): Change resolver result generator to be able to inject
    // the error to be returned.
    result_handler_->ReturnError(grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resolver transient failure"),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
    return_failure_ = false;
  } else if (has_next_result_) {
    Result result;
    result.addresses = std::move(next_result_.addresses);
    result.service_config = std::move(next_result_.service_config);
    // TODO(roth): Use std::move() once grpc_error is converted to C++.
    result.service_config_error = next_result_.service_config_error;
    next_result_.service_config_error = GRPC_ERROR_NONE;
    // When both next_results_ and channel_args_ contain an arg with the same
    // name, only the one in next_results_ will be kept since next_results_ is
    // before channel_args_.
    result.args = grpc_channel_args_union(next_result_.args, channel_args_);
    result_handler_->ReturnResult(std::move(result));
    has_next_result_ = false;
  }
}

void FakeResolver::ReturnReresolutionResult() {
  reresolution_closure_pending_ = false;
  MaybeSendResultLocked();
  Unref();
}

class FakeResolverResponseSetter {
 public:
  explicit FakeResolverResponseSetter(RefCountedPtr<FakeResolver> resolver,
                                      Resolver::Result result,
                                      bool has_result = false,
                                      bool immediate = true)
      : resolver_(std::move(resolver)),
        result_(std::move(result)),
        has_result_(has_result),
        immediate_(immediate) {}
  void SetResponseLocked();
  void SetReresolutionResponseLocked();
  void SetFailureLocked();

 private:
  RefCountedPtr<FakeResolver> resolver_;
  Resolver::Result result_;
  bool has_result_;
  bool immediate_;
};

// Deletes object when done
void FakeResolverResponseSetter::SetReresolutionResponseLocked() {
  if (!resolver_->shutdown_) {
    resolver_->reresolution_result_ = std::move(result_);
    resolver_->has_reresolution_result_ = has_result_;
  }
  delete this;
}

// Deletes object when done
void FakeResolverResponseSetter::SetResponseLocked() {
  if (!resolver_->shutdown_) {
    resolver_->next_result_ = std::move(result_);
    resolver_->has_next_result_ = true;
    resolver_->MaybeSendResultLocked();
  }
  delete this;
}

// Deletes object when done
void FakeResolverResponseSetter::SetFailureLocked() {
  if (!resolver_->shutdown_) {
    resolver_->return_failure_ = true;
    if (immediate_) resolver_->MaybeSendResultLocked();
  }
  delete this;
}

//
// FakeResolverResponseGenerator
//

FakeResolverResponseGenerator::FakeResolverResponseGenerator() {}

FakeResolverResponseGenerator::~FakeResolverResponseGenerator() {}

void FakeResolverResponseGenerator::SetResponse(Resolver::Result result) {
  RefCountedPtr<FakeResolver> resolver;
  {
    MutexLock lock(&mu_);
    if (resolver_ == nullptr) {
      has_result_ = true;
      result_ = std::move(result);
      return;
    }
    resolver = resolver_->Ref();
  }
  FakeResolverResponseSetter* arg =
      new FakeResolverResponseSetter(resolver, std::move(result));
  resolver->work_serializer_->Run([arg]() { arg->SetResponseLocked(); },
                                  DEBUG_LOCATION);
}

void FakeResolverResponseGenerator::SetReresolutionResponse(
    Resolver::Result result) {
  RefCountedPtr<FakeResolver> resolver;
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(resolver_ != nullptr);
    resolver = resolver_->Ref();
  }
  FakeResolverResponseSetter* arg = new FakeResolverResponseSetter(
      resolver, std::move(result), true /* has_result */);
  resolver->work_serializer_->Run(
      [arg]() { arg->SetReresolutionResponseLocked(); }, DEBUG_LOCATION);
}

void FakeResolverResponseGenerator::UnsetReresolutionResponse() {
  RefCountedPtr<FakeResolver> resolver;
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(resolver_ != nullptr);
    resolver = resolver_->Ref();
  }
  FakeResolverResponseSetter* arg =
      new FakeResolverResponseSetter(resolver, Resolver::Result());
  resolver->work_serializer_->Run(
      [arg]() { arg->SetReresolutionResponseLocked(); }, DEBUG_LOCATION);
}

void FakeResolverResponseGenerator::SetFailure() {
  RefCountedPtr<FakeResolver> resolver;
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(resolver_ != nullptr);
    resolver = resolver_->Ref();
  }
  FakeResolverResponseSetter* arg =
      new FakeResolverResponseSetter(resolver, Resolver::Result());
  resolver->work_serializer_->Run([arg]() { arg->SetFailureLocked(); },
                                  DEBUG_LOCATION);
}

void FakeResolverResponseGenerator::SetFailureOnReresolution() {
  RefCountedPtr<FakeResolver> resolver;
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(resolver_ != nullptr);
    resolver = resolver_->Ref();
  }
  FakeResolverResponseSetter* arg = new FakeResolverResponseSetter(
      resolver, Resolver::Result(), false /* has_result */,
      false /* immediate */);
  resolver->work_serializer_->Run([arg]() { arg->SetFailureLocked(); },
                                  DEBUG_LOCATION);
}

void FakeResolverResponseGenerator::SetFakeResolver(
    RefCountedPtr<FakeResolver> resolver) {
  MutexLock lock(&mu_);
  resolver_ = std::move(resolver);
  if (resolver_ == nullptr) return;
  if (has_result_) {
    FakeResolverResponseSetter* arg =
        new FakeResolverResponseSetter(resolver_, std::move(result_));
    resolver_->work_serializer_->Run([arg]() { arg->SetResponseLocked(); },
                                     DEBUG_LOCATION);
    has_result_ = false;
  }
}

namespace {

void* ResponseGeneratorChannelArgCopy(void* p) {
  auto* generator = static_cast<FakeResolverResponseGenerator*>(p);
  generator->Ref().release();
  return p;
}

void ResponseGeneratorChannelArgDestroy(void* p) {
  auto* generator = static_cast<FakeResolverResponseGenerator*>(p);
  generator->Unref();
}

int ResponseGeneratorChannelArgCmp(void* a, void* b) { return GPR_ICMP(a, b); }

}  // namespace

const grpc_arg_pointer_vtable
    FakeResolverResponseGenerator::kChannelArgPointerVtable = {
        ResponseGeneratorChannelArgCopy, ResponseGeneratorChannelArgDestroy,
        ResponseGeneratorChannelArgCmp};

grpc_arg FakeResolverResponseGenerator::MakeChannelArg(
    FakeResolverResponseGenerator* generator) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR), generator,
      &kChannelArgPointerVtable);
}

RefCountedPtr<FakeResolverResponseGenerator>
FakeResolverResponseGenerator::GetFromArgs(const grpc_channel_args* args) {
  auto* response_generator =
      grpc_channel_args_find_pointer<FakeResolverResponseGenerator>(
          args, GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR);
  if (response_generator == nullptr) return nullptr;
  return response_generator->Ref();
}

//
// Factory
//

namespace {

class FakeResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const URI& /*uri*/) const override { return true; }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return MakeOrphanable<FakeResolver>(std::move(args));
  }

  const char* scheme() const override { return "fake"; }
};

}  // namespace

}  // namespace grpc_core

void grpc_resolver_fake_init() {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<grpc_core::FakeResolverFactory>());
}

void grpc_resolver_fake_shutdown() {}
