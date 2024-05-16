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

#include "src/core/resolver/fake/fake_resolver.h"

#include <memory>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/util/useful.h"

namespace grpc_core {

// This cannot be in an anonymous namespace, because it is a friend of
// FakeResolverResponseGenerator.
class FakeResolver final : public Resolver {
 public:
  explicit FakeResolver(ResolverArgs args);

  void StartLocked() override;

  void RequestReresolutionLocked() override;

 private:
  friend class FakeResolverResponseGenerator;

  void ShutdownLocked() override;

  void MaybeSendResultLocked();

  // passed-in parameters
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
  ChannelArgs channel_args_;
  RefCountedPtr<FakeResolverResponseGenerator> response_generator_;
  // The next resolution result to be returned, if any.  Present when we
  // get a result before the resolver is started.
  absl::optional<Result> next_result_;
  // True after the call to StartLocked().
  bool started_ = false;
  // True after the call to ShutdownLocked().
  bool shutdown_ = false;
};

FakeResolver::FakeResolver(ResolverArgs args)
    : work_serializer_(std::move(args.work_serializer)),
      result_handler_(std::move(args.result_handler)),
      channel_args_(
          // Channels sharing the same subchannels may have different resolver
          // response generators. If we don't remove this arg, subchannel pool
          // will create new subchannels for the same address instead of
          // reusing existing ones because of different values of this channel
          // arg. Can't just use GRPC_ARG_NO_SUBCHANNEL_PREFIX, since
          // that can't be passed into the channel from test code.
          args.args.Remove(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR)),
      response_generator_(
          args.args.GetObjectRef<FakeResolverResponseGenerator>()) {
  if (response_generator_ != nullptr) {
    response_generator_->SetFakeResolver(RefAsSubclass<FakeResolver>());
  }
}

void FakeResolver::StartLocked() {
  started_ = true;
  MaybeSendResultLocked();
}

void FakeResolver::RequestReresolutionLocked() {
  // Re-resolution can't happen until after we return an initial result.
  CHECK(response_generator_ != nullptr);
  response_generator_->ReresolutionRequested();
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
  if (next_result_.has_value()) {
    // When both next_results_ and channel_args_ contain an arg with the same
    // name, use the one in next_results_.
    next_result_->args = next_result_->args.UnionWith(channel_args_);
    result_handler_->ReportResult(std::move(*next_result_));
    next_result_.reset();
  }
}

//
// FakeResolverResponseGenerator
//

FakeResolverResponseGenerator::FakeResolverResponseGenerator() {}

FakeResolverResponseGenerator::~FakeResolverResponseGenerator() {}

void FakeResolverResponseGenerator::SetResponseAndNotify(
    Resolver::Result result, Notification* notify_when_set) {
  RefCountedPtr<FakeResolver> resolver;
  {
    MutexLock lock(&mu_);
    if (resolver_ == nullptr) {
      result_ = std::move(result);
      if (notify_when_set != nullptr) notify_when_set->Notify();
      return;
    }
    resolver = resolver_;
  }
  SendResultToResolver(std::move(resolver), std::move(result), notify_when_set);
}

void FakeResolverResponseGenerator::SetFakeResolver(
    RefCountedPtr<FakeResolver> resolver) {
  Resolver::Result result;
  {
    MutexLock lock(&mu_);
    resolver_ = resolver;
    if (resolver_set_cv_ != nullptr) resolver_set_cv_->SignalAll();
    if (resolver == nullptr) return;
    if (!result_.has_value()) return;
    result = std::move(*result_);
    result_.reset();
  }
  SendResultToResolver(std::move(resolver), std::move(result), nullptr);
}

void FakeResolverResponseGenerator::SendResultToResolver(
    RefCountedPtr<FakeResolver> resolver, Resolver::Result result,
    Notification* notify_when_set) {
  auto* resolver_ptr = resolver.get();
  resolver_ptr->work_serializer_->Run(
      [resolver = std::move(resolver), result = std::move(result),
       notify_when_set]() mutable {
        if (!resolver->shutdown_) {
          resolver->next_result_ = std::move(result);
          resolver->MaybeSendResultLocked();
        }
        if (notify_when_set != nullptr) notify_when_set->Notify();
      },
      DEBUG_LOCATION);
}

bool FakeResolverResponseGenerator::WaitForResolverSet(absl::Duration timeout) {
  MutexLock lock(&mu_);
  if (resolver_ == nullptr) {
    CondVar condition;
    resolver_set_cv_ = &condition;
    condition.WaitWithTimeout(&mu_, timeout);
    resolver_set_cv_ = nullptr;
  }
  return resolver_ != nullptr;
}

bool FakeResolverResponseGenerator::WaitForReresolutionRequest(
    absl::Duration timeout) {
  MutexLock lock(&reresolution_mu_);
  if (!reresolution_requested_) {
    CondVar condition;
    reresolution_cv_ = &condition;
    condition.WaitWithTimeout(&reresolution_mu_, timeout);
    reresolution_cv_ = nullptr;
  }
  return std::exchange(reresolution_requested_, false);
}

void FakeResolverResponseGenerator::ReresolutionRequested() {
  MutexLock lock(&reresolution_mu_);
  reresolution_requested_ = true;
  if (reresolution_cv_ != nullptr) reresolution_cv_->SignalAll();
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

int ResponseGeneratorChannelArgCmp(void* a, void* b) {
  return QsortCompare(a, b);
}

}  // namespace

const grpc_arg_pointer_vtable
    FakeResolverResponseGenerator::kChannelArgPointerVtable = {
        ResponseGeneratorChannelArgCopy, ResponseGeneratorChannelArgDestroy,
        ResponseGeneratorChannelArgCmp};

//
// Factory
//

namespace {

class FakeResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "fake"; }

  bool IsValidUri(const URI& /*uri*/) const override { return true; }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return MakeOrphanable<FakeResolver>(std::move(args));
  }
};

}  // namespace

void RegisterFakeResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<FakeResolverFactory>());
}

}  // namespace grpc_core

void grpc_resolver_fake_shutdown() {}
