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

#ifndef GRPC_TEST_CORE_END2END_END2END_TESTS_H
#define GRPC_TEST_CORE_END2END_END2END_TESTS_H

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "cq_verifier.h"
#include "gtest/gtest.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

// Test feature flags.
#define FEATURE_MASK_SUPPORTS_HOSTNAME_VERIFICATION 2
// Feature mask supports call credentials with a minimum security level of
// GRPC_PRIVACY_AND_INTEGRITY.
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS 4
// Feature mask supports call credentials with a minimum security level of
// GRPC_SECURTITY_NONE.
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE 8
#define FEATURE_MASK_SUPPORTS_REQUEST_PROXYING 16
#define FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL 32
#define FEATURE_MASK_IS_HTTP2 64
#define FEATURE_MASK_ENABLES_TRACES 128
#define FEATURE_MASK_1BYTE_AT_A_TIME 256
#define FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST 1024
#define FEATURE_MASK_DOES_NOT_SUPPORT_DEADLINES 2048

#define FAIL_AUTH_CHECK_SERVER_ARG_NAME "fail_auth_check"

namespace grpc_core {
class CoreTestFixture {
 public:
  virtual ~CoreTestFixture() {
    ShutdownServer();
    ShutdownClient();
    grpc_completion_queue_shutdown(cq());
    DrainCq();
    grpc_completion_queue_destroy(cq());
  }

  grpc_completion_queue* cq() { return cq_; }
  grpc_server* server() { return server_; }
  grpc_channel* client() { return client_; }

  void InitServer(const ChannelArgs& args) {
    if (server_ != nullptr) ShutdownServer();
    server_ = MakeServer(args);
    GPR_ASSERT(server_ != nullptr);
  }
  void InitClient(const ChannelArgs& args) {
    if (client_ != nullptr) ShutdownClient();
    client_ = MakeClient(args);
    GPR_ASSERT(client_ != nullptr);
  }

  void ShutdownServer() {
    if (server_ == nullptr) return;
    grpc_server_shutdown_and_notify(server_, cq_, server_);
    grpc_event ev;
    do {
      ev = grpc_completion_queue_next(cq_, grpc_timeout_seconds_to_deadline(5),
                                      nullptr);
    } while (ev.type != GRPC_OP_COMPLETE || ev.tag != server_);
    DestroyServer();
  }

  void DestroyServer() {
    if (server_ == nullptr) return;
    grpc_server_destroy(server_);
    server_ = nullptr;
  }

  void ShutdownClient() {
    if (client_ == nullptr) return;
    grpc_channel_destroy(client_);
    client_ = nullptr;
  }

 protected:
  void SetServer(grpc_server* server);
  void SetClient(grpc_channel* client);

 private:
  virtual grpc_server* MakeServer(const ChannelArgs& args) = 0;
  virtual grpc_channel* MakeClient(const ChannelArgs& args) = 0;

  void DrainCq() {
    grpc_event ev;
    do {
      ev = grpc_completion_queue_next(cq_, grpc_timeout_seconds_to_deadline(5),
                                      nullptr);
    } while (ev.type != GRPC_QUEUE_SHUTDOWN);
  }

  grpc_completion_queue* cq_ = grpc_completion_queue_create_for_next(nullptr);
  grpc_server* server_ = nullptr;
  grpc_channel* client_ = nullptr;
};

Slice RandomSlice(size_t length);
Slice RandomBinarySlice(size_t length);
using ByteBufferUniquePtr =
    std::unique_ptr<grpc_byte_buffer, void (*)(grpc_byte_buffer*)>;
ByteBufferUniquePtr ByteBufferFromSlice(Slice slice);

struct CoreTestConfiguration {
  // A descriptive name for this test fixture.
  const char* name;

  // Which features are supported by this fixture. See feature flags above.
  uint32_t feature_mask;

  // If the call host is setup by the fixture (for example, via the
  // GRPC_SSL_TARGET_NAME_OVERRIDE_ARG channel arg), which value should the
  // test expect to find in call_details.host
  const char* overridden_call_host;

  std::function<std::unique_ptr<CoreTestFixture>(
      const ChannelArgs& client_args, const ChannelArgs& server_args)>
      create_fixture;
};

class CoreEnd2endTest
    : public ::testing::TestWithParam<const CoreTestConfiguration*> {
 public:
  void SetUp() override;
  void TearDown() override;

  class Call;

  class ClientCallBuilder {
   public:
    ClientCallBuilder(CoreEnd2endTest& test, std::string method)
        : test_(test), method_(std::move(method)) {}

    ClientCallBuilder& Host(std::string host) {
      host_ = std::move(host);
      return *this;
    }
    ClientCallBuilder& Timeout(Duration timeout) {
      if (timeout == Duration::Infinity()) {
        deadline_ = gpr_inf_future(GPR_CLOCK_REALTIME);
        return *this;
      }
      deadline_ = grpc_timeout_milliseconds_to_deadline(timeout.millis());
      return *this;
    }
    Call Create();

   private:
    CoreEnd2endTest& test_;
    const std::string method_;
    absl::optional<std::string> host_;
    grpc_call* parent_call_ = nullptr;
    uint32_t propagation_mask_ = GRPC_PROPAGATE_DEFAULTS;
    gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_REALTIME);
  };

  class IncomingMetadata {
   public:
    IncomingMetadata() = default;
    IncomingMetadata(const IncomingMetadata&) = delete;
    IncomingMetadata& operator=(const IncomingMetadata&) = delete;
    ~IncomingMetadata() { grpc_metadata_array_destroy(&metadata_); }

    absl::optional<absl::string_view> Get(absl::string_view key) const;

    grpc_op MakeOp();

   private:
    grpc_metadata_array metadata_{0, 0, nullptr};
  };

  class IncomingMessage {
   public:
    IncomingMessage() = default;
    IncomingMessage(const IncomingMessage&) = delete;
    IncomingMessage& operator=(const IncomingMessage&) = delete;
    ~IncomingMessage() {
      if (payload_ != nullptr) grpc_byte_buffer_destroy(payload_);
    }

    Slice payload() const;

    grpc_op MakeOp();

   private:
    grpc_byte_buffer* payload_ = nullptr;
  };

  class IncomingStatusOnClient {
   public:
    IncomingStatusOnClient() = default;
    IncomingStatusOnClient(const IncomingStatusOnClient&) = delete;
    IncomingStatusOnClient& operator=(const IncomingStatusOnClient&) = delete;
    ~IncomingStatusOnClient() {
      grpc_metadata_array_destroy(&trailing_metadata_);
      gpr_free(const_cast<char*>(error_string_));
    }

    grpc_status_code status() const { return status_; }
    absl::string_view message() const {
      return status_details_.as_string_view();
    }
    absl::optional<absl::string_view> GetTrailingMetadata(
        absl::string_view key) const;

    grpc_op MakeOp();

   private:
    grpc_metadata_array trailing_metadata_{0, 0, nullptr};
    grpc_status_code status_;
    Slice status_details_;
    const char* error_string_ = nullptr;
  };

  class IncomingCloseOnServer {
   public:
    IncomingCloseOnServer() = default;
    IncomingCloseOnServer(const IncomingCloseOnServer&) = delete;
    IncomingCloseOnServer& operator=(const IncomingCloseOnServer&) = delete;

    bool was_cancelled() const { return cancelled_ != 0; }

    grpc_op MakeOp();

   private:
    int cancelled_;
  };

  class BatchBuilder {
   public:
    BatchBuilder(grpc_call* call, int tag) : call_(call), tag_(tag) {}
    ~BatchBuilder();

    BatchBuilder(const BatchBuilder&) = delete;
    BatchBuilder& operator=(const BatchBuilder&) = delete;
    BatchBuilder(BatchBuilder&&) noexcept = default;

    BatchBuilder& SendInitialMetadata(
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            md);

    BatchBuilder& SendMessage(Slice payload);
    BatchBuilder& SendMessage(absl::string_view payload) {
      return SendMessage(Slice::FromCopiedString(payload));
    }

    BatchBuilder& SendCloseFromClient();

    BatchBuilder& SendStatusFromServer(
        grpc_status_code status, absl::string_view message,
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            md);

    BatchBuilder& RecvInitialMetadata(IncomingMetadata& md) {
      ops_.emplace_back(md.MakeOp());
      return *this;
    }

    BatchBuilder& RecvMessage(IncomingMessage& msg) {
      ops_.emplace_back(msg.MakeOp());
      return *this;
    }

    BatchBuilder& RecvStatusOnClient(IncomingStatusOnClient& status) {
      ops_.emplace_back(status.MakeOp());
      return *this;
    }

    BatchBuilder& RecvCloseOnServer(IncomingCloseOnServer& close) {
      ops_.emplace_back(close.MakeOp());
      return *this;
    }

   private:
    class Thing {
     public:
      virtual ~Thing() = default;
    };
    template <typename T>
    class SpecificThing final : public Thing {
     public:
      template <typename... Args>
      explicit SpecificThing(Args&&... args)
          : t_(std::forward<Args>(args)...) {}
      SpecificThing() = default;

      T& get() { return t_; }

     private:
      T t_;
    };

    template <typename T, typename... Args>
    T& Make(Args&&... args) {
      things_.emplace_back(new SpecificThing<T>(std::forward<Args>(args)...));
      return static_cast<SpecificThing<T>*>(things_.back().get())->get();
    }

    grpc_call* call_;
    const int tag_;
    std::vector<grpc_op> ops_;
    std::vector<std::unique_ptr<Thing>> things_;
  };

  class Call {
   public:
    explicit Call(grpc_call* call) : call_(call) {}
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;
    Call(Call&& other) noexcept : call_(std::exchange(other.call_, nullptr)) {}
    ~Call() { grpc_call_unref(call_); }
    BatchBuilder NewBatch(int tag) { return BatchBuilder(call_, tag); }
    void Cancel() { grpc_call_cancel(call_, nullptr); }

    grpc_call** call_ptr() { return &call_; }
    grpc_call* c_call() const { return call_; }

   private:
    grpc_call* call_;
  };

  class IncomingCall {
   public:
    IncomingCall(CoreEnd2endTest& test, int tag);
    IncomingCall(const IncomingCall&) = delete;
    IncomingCall& operator=(const IncomingCall&) = delete;
    IncomingCall(IncomingCall&&) noexcept = default;

    BatchBuilder NewBatch(int tag) { return impl_->call.NewBatch(tag); }
    void Cancel() { impl_->call.Cancel(); }

    absl::string_view method() const {
      return StringViewFromSlice(impl_->call_details.method);
    }

    absl::optional<absl::string_view> GetInitialMetadata(
        absl::string_view key) const;

   private:
    struct Impl {
      Impl() {
        grpc_call_details_init(&call_details);
        grpc_metadata_array_init(&request_metadata);
      }
      ~Impl() {
        grpc_call_details_destroy(&call_details);
        grpc_metadata_array_destroy(&request_metadata);
      }
      Call call{nullptr};
      grpc_call_details call_details;
      grpc_metadata_array request_metadata;
    };
    std::unique_ptr<Impl> impl_;
  };

  ClientCallBuilder NewClientCall(std::string method) {
    return ClientCallBuilder(*this, std::move(method));
  }
  IncomingCall RequestCall(int tag) { return IncomingCall(*this, tag); }

  using ExpectedResult = CqVerifier::ExpectedResult;
  using Maybe = CqVerifier::Maybe;
  using AnyStatus = CqVerifier::AnyStatus;
  void Expect(int tag, ExpectedResult result, SourceLocation whence = {}) {
    expectations_++;
    cq_verifier_->Expect(CqVerifier::tag(tag), result, whence);
  }
  void Step() {
    if (expectations_ == 0) {
      cq_verifier_->VerifyEmpty();
      return;
    }
    expectations_ = 0;
    cq_verifier_->Verify();
  }

  void InitClient(const ChannelArgs& args) {
    initialized_ = true;
    fixture_->InitClient(args);
  }
  void InitServer(const ChannelArgs& args) {
    initialized_ = true;
    fixture_->InitServer(args);
  }
  void ShutdownServerAndNotify(int tag) {
    grpc_server_shutdown_and_notify(fixture_->server(), fixture_->cq(),
                                    CqVerifier::tag(tag));
  }

 private:
  void ForceInitialized();

  std::unique_ptr<CoreTestFixture> fixture_;
  std::unique_ptr<CqVerifier> cq_verifier_;
  int expectations_ = 0;
  bool initialized_ = false;
};

class CoreLargeSendTest : public CoreEnd2endTest {};
class CoreClientChannelTest : public CoreEnd2endTest {};
class CoreDeadlineTest : public CoreEnd2endTest {};
class HpackSizeTest : public CoreEnd2endTest {};

}  // namespace grpc_core

void grpc_end2end_tests_pre_init(void);
void grpc_end2end_tests(int argc, char** argv,
                        const grpc_core::CoreTestConfiguration& config);

const char* get_host_override_string(
    const char* str, const grpc_core::CoreTestConfiguration& config);
// Returns a pointer to a statically allocated slice: future invocations
// overwrite past invocations, not threadsafe, etc...
const grpc_slice* get_host_override_slice(
    const char* str, const grpc_core::CoreTestConfiguration& config);

void validate_host_override_string(
    const char* pattern, grpc_slice str,
    const grpc_core::CoreTestConfiguration& config);

#endif  // GRPC_TEST_CORE_END2END_END2END_TESTS_H
