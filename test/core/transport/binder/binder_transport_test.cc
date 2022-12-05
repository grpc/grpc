// Copyright 2021 gRPC authors.
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

// Unit-tests for grpc_binder_transport
//
// Verify that a calls to the perform_stream_op of grpc_binder_transport
// transform into the correct sequence of binder transactions.
#include "src/core/ext/transport/binder/transport/binder_transport.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"

#include <grpc/grpc.h>
#include <grpcpp/security/binder_security_policy.h>

#include "src/core/ext/transport/binder/transport/binder_stream.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/transport/binder/mock_objects.h"
#include "test/core/util/test_config.h"

namespace grpc_binder {
namespace {

using ::testing::Expectation;
using ::testing::NiceMock;
using ::testing::Return;

class BinderTransportTest : public ::testing::Test {
 public:
  BinderTransportTest()
      : transport_(grpc_create_binder_transport_client(
            std::make_unique<NiceMock<MockBinder>>(),
            std::make_shared<
                grpc::experimental::binder::UntrustedSecurityPolicy>())) {
    auto* gbt = reinterpret_cast<grpc_binder_transport*>(transport_);
    gbt->wire_writer = std::make_unique<MockWireWriter>();
    GRPC_STREAM_REF_INIT(&ref_, 1, nullptr, nullptr, "phony ref");
  }

  ~BinderTransportTest() override {
    grpc_core::ExecCtx exec_ctx;
    grpc_transport_destroy(transport_);
    grpc_core::ExecCtx::Get()->Flush();
    for (grpc_binder_stream* gbs : stream_buffer_) {
      gbs->~grpc_binder_stream();
      gpr_free(gbs);
    }
    arena_->Destroy();
  }

  void PerformStreamOp(grpc_binder_stream* gbs,
                       grpc_transport_stream_op_batch* op) {
    grpc_transport_perform_stream_op(transport_,
                                     reinterpret_cast<grpc_stream*>(gbs), op);
  }

  grpc_binder_transport* GetBinderTransport() {
    return reinterpret_cast<grpc_binder_transport*>(transport_);
  }

  grpc_binder_stream* InitNewBinderStream() {
    grpc_binder_stream* gbs = static_cast<grpc_binder_stream*>(
        gpr_malloc(grpc_transport_stream_size(transport_)));
    grpc_transport_init_stream(transport_, reinterpret_cast<grpc_stream*>(gbs),
                               &ref_, nullptr, arena_);
    stream_buffer_.push_back(gbs);
    return gbs;
  }

  MockWireWriter& GetWireWriter() {
    return *reinterpret_cast<MockWireWriter*>(
        GetBinderTransport()->wire_writer.get());
  }

  static void SetUpTestSuite() { grpc_init(); }
  static void TearDownTestSuite() { grpc_shutdown(); }

 protected:
  grpc_core::MemoryAllocator memory_allocator_ =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  grpc_core::Arena* arena_ =
      grpc_core::Arena::Create(/* initial_size = */ 1, &memory_allocator_);
  grpc_transport* transport_;
  grpc_stream_refcount ref_;
  std::vector<grpc_binder_stream*> stream_buffer_;
};

void MockCallback(void* arg, grpc_error_handle error);

class MockGrpcClosure {
 public:
  explicit MockGrpcClosure(grpc_core::Notification* notification = nullptr)
      : notification_(notification) {
    GRPC_CLOSURE_INIT(&closure_, MockCallback, this, nullptr);
  }

  grpc_closure* GetGrpcClosure() { return &closure_; }
  MOCK_METHOD(void, Callback, (grpc_error_handle), ());

  grpc_core::Notification* notification_;

 private:
  grpc_closure closure_;
};

void MockCallback(void* arg, grpc_error_handle error) {
  MockGrpcClosure* mock_closure = static_cast<MockGrpcClosure*>(arg);
  mock_closure->Callback(error);
  if (mock_closure->notification_) {
    mock_closure->notification_->Notify();
  }
}

std::string MetadataString(const Metadata& a) {
  return absl::StrCat(
      "{",
      absl::StrJoin(
          a, ", ",
          [](std::string* out, const std::pair<std::string, std::string>& kv) {
            out->append(
                absl::StrCat("\"", kv.first, "\": \"", kv.second, "\""));
          }),
      "}");
}

bool MetadataEquivalent(Metadata a, Metadata b) {
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  return a == b;
}

// Matches with transactions having the desired flag, method_ref,
// initial_metadata, and message_data.
MATCHER_P4(TransactionMatches, flag, method_ref, initial_metadata, message_data,
           "") {
  if (arg->GetFlags() != flag) return false;
  if (flag & kFlagPrefix) {
    if (arg->GetMethodRef() != method_ref) {
      printf("METHOD REF NOT EQ: %s %s\n",
             std::string(arg->GetMethodRef()).c_str(),
             std::string(method_ref).c_str());
      return false;
    }
    if (!MetadataEquivalent(arg->GetPrefixMetadata(), initial_metadata)) {
      printf("METADATA NOT EQUIVALENT: %s %s\n",
             MetadataString(arg->GetPrefixMetadata()).c_str(),
             MetadataString(initial_metadata).c_str());
      return false;
    }
  }
  if (flag & kFlagMessageData) {
    if (arg->GetMessageData() != message_data) return false;
  }
  return true;
}

// Matches with grpc_error having error message containing |msg|.
MATCHER_P(GrpcErrorMessageContains, msg, "") {
  return absl::StrContains(grpc_core::StatusToString(arg), msg);
}

namespace {
class MetadataEncoder {
 public:
  void Encode(const grpc_core::Slice& key, const grpc_core::Slice& value) {
    metadata_.emplace_back(std::string(key.as_string_view()),
                           std::string(value.as_string_view()));
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    metadata_.emplace_back(
        std::string(Which::key()),
        // NOLINTNEXTLINE(google-readability-casting)
        std::string(grpc_core::Slice(Which::Encode(value)).as_string_view()));
  }

  const Metadata& metadata() const { return metadata_; }

 private:
  Metadata metadata_;
};
}  // namespace

// Verify that the lower-level metadata has the same content as the gRPC
// metadata.
void VerifyMetadataEqual(const Metadata& md,
                         const grpc_metadata_batch& grpc_md) {
  MetadataEncoder encoder;
  grpc_md.Encode(&encoder);
  EXPECT_TRUE(MetadataEquivalent(encoder.metadata(), md));
}

// RAII helper classes for constructing gRPC metadata and receiving callbacks.
struct MakeSendInitialMetadata {
  MakeSendInitialMetadata(const Metadata& initial_metadata,
                          const std::string& method_ref,
                          grpc_transport_stream_op_batch* op) {
    for (const auto& md : initial_metadata) {
      const std::string& key = md.first;
      const std::string& value = md.second;
      grpc_initial_metadata.Append(
          key, grpc_core::Slice::FromCopiedString(value),
          [](absl::string_view, const grpc_core::Slice&) { abort(); });
    }
    if (!method_ref.empty()) {
      grpc_initial_metadata.Set(grpc_core::HttpPathMetadata(),
                                grpc_core::Slice::FromCopiedString(method_ref));
    }
    op->send_initial_metadata = true;
    op->payload->send_initial_metadata.send_initial_metadata =
        &grpc_initial_metadata;
  }
  ~MakeSendInitialMetadata() {}

  grpc_core::MemoryAllocator memory_allocator =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  grpc_core::ScopedArenaPtr arena =
      grpc_core::MakeScopedArena(1024, &memory_allocator);
  grpc_metadata_batch grpc_initial_metadata{arena.get()};
};

struct MakeSendMessage {
  MakeSendMessage(const std::string& message,
                  grpc_transport_stream_op_batch* op) {
    send_stream.Append(grpc_core::Slice::FromCopiedString(message));

    op->send_message = true;
    op->payload->send_message.send_message = &send_stream;
  }

  grpc_core::SliceBuffer send_stream;
};

struct MakeSendTrailingMetadata {
  explicit MakeSendTrailingMetadata(const Metadata& trailing_metadata,
                                    grpc_transport_stream_op_batch* op) {
    EXPECT_TRUE(trailing_metadata.empty());

    op->send_trailing_metadata = true;
    op->payload->send_trailing_metadata.send_trailing_metadata =
        &grpc_trailing_metadata;
  }

  grpc_core::MemoryAllocator memory_allocator =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  grpc_core::ScopedArenaPtr arena =
      grpc_core::MakeScopedArena(1024, &memory_allocator);
  grpc_metadata_batch grpc_trailing_metadata{arena.get()};
};

struct MakeRecvInitialMetadata {
  explicit MakeRecvInitialMetadata(grpc_transport_stream_op_batch* op,
                                   Expectation* call_before = nullptr)
      : ready(&notification) {
    op->recv_initial_metadata = true;
    op->payload->recv_initial_metadata.recv_initial_metadata =
        &grpc_initial_metadata;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        ready.GetGrpcClosure();
    if (call_before) {
      EXPECT_CALL(ready, Callback).After(*call_before);
    } else {
      EXPECT_CALL(ready, Callback);
    }
  }

  ~MakeRecvInitialMetadata() {}

  MockGrpcClosure ready;
  grpc_core::MemoryAllocator memory_allocator =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  grpc_core::ScopedArenaPtr arena =
      grpc_core::MakeScopedArena(1024, &memory_allocator);
  grpc_metadata_batch grpc_initial_metadata{arena.get()};
  grpc_core::Notification notification;
};

struct MakeRecvMessage {
  explicit MakeRecvMessage(grpc_transport_stream_op_batch* op,
                           Expectation* call_before = nullptr)
      : ready(&notification) {
    op->recv_message = true;
    op->payload->recv_message.recv_message = &grpc_message;
    op->payload->recv_message.recv_message_ready = ready.GetGrpcClosure();
    if (call_before) {
      EXPECT_CALL(ready, Callback).After(*call_before);
    } else {
      EXPECT_CALL(ready, Callback);
    }
  }

  MockGrpcClosure ready;
  grpc_core::Notification notification;
  absl::optional<grpc_core::SliceBuffer> grpc_message;
};

struct MakeRecvTrailingMetadata {
  explicit MakeRecvTrailingMetadata(grpc_transport_stream_op_batch* op,
                                    Expectation* call_before = nullptr)
      : ready(&notification) {
    op->recv_trailing_metadata = true;
    op->payload->recv_trailing_metadata.recv_trailing_metadata =
        &grpc_trailing_metadata;
    op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        ready.GetGrpcClosure();
    if (call_before) {
      EXPECT_CALL(ready, Callback).After(*call_before);
    } else {
      EXPECT_CALL(ready, Callback);
    }
  }

  ~MakeRecvTrailingMetadata() {}

  MockGrpcClosure ready;
  grpc_core::MemoryAllocator memory_allocator =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  grpc_core::ScopedArenaPtr arena =
      grpc_core::MakeScopedArena(1024, &memory_allocator);
  grpc_metadata_batch grpc_trailing_metadata{arena.get()};
  grpc_core::Notification notification;
};

const Metadata kDefaultMetadata = {
    {"", ""},
    {"", "value"},
    {"key", ""},
    {"key", "value"},
};

constexpr char kDefaultMethodRef[] = "/some/path";
constexpr char kDefaultMessage[] = "binder transport message";
constexpr int kDefaultStatus = 0x1234;

Metadata AppendMethodRef(const Metadata& md, const std::string& method_ref) {
  Metadata result = md;
  result.emplace_back(":path", method_ref);
  return result;
}

Metadata AppendStatus(const Metadata& md, int status) {
  Metadata result = md;
  result.emplace_back("grpc-status", std::to_string(status));
  return result;
}

}  // namespace

TEST_F(BinderTransportTest, CreateBinderTransport) {
  EXPECT_NE(transport_, nullptr);
}

TEST_F(BinderTransportTest, TransactionIdIncrement) {
  grpc_binder_stream* gbs0 = InitNewBinderStream();
  EXPECT_EQ(gbs0->t, GetBinderTransport());
  EXPECT_EQ(gbs0->tx_code, kFirstCallId);
  grpc_binder_stream* gbs1 = InitNewBinderStream();
  EXPECT_EQ(gbs1->t, GetBinderTransport());
  EXPECT_EQ(gbs1->tx_code, kFirstCallId + 1);
  grpc_binder_stream* gbs2 = InitNewBinderStream();
  EXPECT_EQ(gbs2->t, GetBinderTransport());
  EXPECT_EQ(gbs2->tx_code, kFirstCallId + 2);
}

TEST_F(BinderTransportTest, PerformSendInitialMetadata) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;
  const Metadata kInitialMetadata = kDefaultMetadata;
  MakeSendInitialMetadata send_initial_metadata(kInitialMetadata, "", &op);
  MockGrpcClosure mock_on_complete;
  op.on_complete = mock_on_complete.GetGrpcClosure();

  ::testing::InSequence sequence;
  EXPECT_CALL(GetWireWriter(), RpcCall(TransactionMatches(
                                   kFlagPrefix, "", kInitialMetadata, "")));
  EXPECT_CALL(mock_on_complete, Callback);

  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
}

TEST_F(BinderTransportTest, PerformSendInitialMetadataMethodRef) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;
  const Metadata kInitialMetadata = kDefaultMetadata;
  const std::string kMethodRef = kDefaultMethodRef;
  MakeSendInitialMetadata send_initial_metadata(kInitialMetadata, kMethodRef,
                                                &op);
  MockGrpcClosure mock_on_complete;
  op.on_complete = mock_on_complete.GetGrpcClosure();

  ::testing::InSequence sequence;
  EXPECT_CALL(GetWireWriter(),
              RpcCall(TransactionMatches(kFlagPrefix, kMethodRef.substr(1),
                                         kInitialMetadata, "")));
  EXPECT_CALL(mock_on_complete, Callback);

  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
}

TEST_F(BinderTransportTest, PerformSendMessage) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;

  const std::string kMessage = kDefaultMessage;
  MakeSendMessage send_message(kMessage, &op);
  MockGrpcClosure mock_on_complete;
  op.on_complete = mock_on_complete.GetGrpcClosure();

  ::testing::InSequence sequence;
  EXPECT_CALL(
      GetWireWriter(),
      RpcCall(TransactionMatches(kFlagMessageData, "", Metadata{}, kMessage)));
  EXPECT_CALL(mock_on_complete, Callback);

  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
}

TEST_F(BinderTransportTest, PerformSendTrailingMetadata) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;
  // The wireformat guarantees that suffix metadata will always be empty.
  // TODO(waynetu): Check whether gRPC can internally add extra trailing
  // metadata.
  const Metadata kTrailingMetadata = {};
  MakeSendTrailingMetadata send_trailing_metadata(kTrailingMetadata, &op);
  MockGrpcClosure mock_on_complete;
  op.on_complete = mock_on_complete.GetGrpcClosure();

  ::testing::InSequence sequence;
  EXPECT_CALL(GetWireWriter(), RpcCall(TransactionMatches(
                                   kFlagSuffix, "", kTrailingMetadata, "")));
  EXPECT_CALL(mock_on_complete, Callback);

  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
}

TEST_F(BinderTransportTest, PerformSendAll) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;

  const Metadata kInitialMetadata = kDefaultMetadata;
  const std::string kMethodRef = kDefaultMethodRef;
  MakeSendInitialMetadata send_initial_metadata(kInitialMetadata, kMethodRef,
                                                &op);

  const std::string kMessage = kDefaultMessage;
  MakeSendMessage send_message(kMessage, &op);

  // The wireformat guarantees that suffix metadata will always be empty.
  // TODO(waynetu): Check whether gRPC can internally add extra trailing
  // metadata.
  const Metadata kTrailingMetadata = {};
  MakeSendTrailingMetadata send_trailing_metadata(kTrailingMetadata, &op);

  MockGrpcClosure mock_on_complete;
  op.on_complete = mock_on_complete.GetGrpcClosure();

  ::testing::InSequence sequence;
  EXPECT_CALL(GetWireWriter(),
              RpcCall(TransactionMatches(
                  kFlagPrefix | kFlagMessageData | kFlagSuffix,
                  kMethodRef.substr(1), kInitialMetadata, kMessage)));
  EXPECT_CALL(mock_on_complete, Callback);

  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
}

TEST_F(BinderTransportTest, PerformRecvInitialMetadata) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;

  MakeRecvInitialMetadata recv_initial_metadata(&op);

  const Metadata kInitialMetadata = kDefaultMetadata;
  auto* gbt = reinterpret_cast<grpc_binder_transport*>(transport_);
  gbt->transport_stream_receiver->NotifyRecvInitialMetadata(gbs->tx_code,
                                                            kInitialMetadata);
  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
  recv_initial_metadata.notification.WaitForNotification();

  VerifyMetadataEqual(kInitialMetadata,
                      recv_initial_metadata.grpc_initial_metadata);
}

TEST_F(BinderTransportTest, PerformRecvInitialMetadataWithMethodRef) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;

  MakeRecvInitialMetadata recv_initial_metadata(&op);

  auto* gbt = reinterpret_cast<grpc_binder_transport*>(transport_);
  const Metadata kInitialMetadataWithMethodRef =
      AppendMethodRef(kDefaultMetadata, kDefaultMethodRef);
  gbt->transport_stream_receiver->NotifyRecvInitialMetadata(
      gbs->tx_code, kInitialMetadataWithMethodRef);
  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
  recv_initial_metadata.notification.WaitForNotification();

  VerifyMetadataEqual(kInitialMetadataWithMethodRef,
                      recv_initial_metadata.grpc_initial_metadata);
}

TEST_F(BinderTransportTest, PerformRecvMessage) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;

  MakeRecvMessage recv_message(&op);

  auto* gbt = reinterpret_cast<grpc_binder_transport*>(transport_);
  const std::string kMessage = kDefaultMessage;
  gbt->transport_stream_receiver->NotifyRecvMessage(gbs->tx_code, kMessage);

  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
  recv_message.notification.WaitForNotification();

  EXPECT_EQ(kMessage, recv_message.grpc_message->JoinIntoString());
}

TEST_F(BinderTransportTest, PerformRecvTrailingMetadata) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;

  MakeRecvTrailingMetadata recv_trailing_metadata(&op);

  const Metadata kTrailingMetadata = kDefaultMetadata;
  auto* gbt = reinterpret_cast<grpc_binder_transport*>(transport_);
  constexpr int kStatus = kDefaultStatus;
  gbt->transport_stream_receiver->NotifyRecvTrailingMetadata(
      gbs->tx_code, kTrailingMetadata, kStatus);
  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
  recv_trailing_metadata.notification.WaitForNotification();

  VerifyMetadataEqual(AppendStatus(kTrailingMetadata, kStatus),
                      recv_trailing_metadata.grpc_trailing_metadata);
}

TEST_F(BinderTransportTest, PerformRecvAll) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;

  MakeRecvInitialMetadata recv_initial_metadata(&op);
  MakeRecvMessage recv_message(&op);
  MakeRecvTrailingMetadata recv_trailing_metadata(&op);

  auto* gbt = reinterpret_cast<grpc_binder_transport*>(transport_);
  const Metadata kInitialMetadataWithMethodRef =
      AppendMethodRef(kDefaultMetadata, kDefaultMethodRef);
  gbt->transport_stream_receiver->NotifyRecvInitialMetadata(
      gbs->tx_code, kInitialMetadataWithMethodRef);

  const std::string kMessage = kDefaultMessage;
  gbt->transport_stream_receiver->NotifyRecvMessage(gbs->tx_code, kMessage);

  Metadata trailing_metadata = kDefaultMetadata;
  constexpr int kStatus = kDefaultStatus;
  gbt->transport_stream_receiver->NotifyRecvTrailingMetadata(
      gbs->tx_code, trailing_metadata, kStatus);
  PerformStreamOp(gbs, &op);
  grpc_core::ExecCtx::Get()->Flush();
  recv_trailing_metadata.notification.WaitForNotification();

  VerifyMetadataEqual(kInitialMetadataWithMethodRef,
                      recv_initial_metadata.grpc_initial_metadata);
  trailing_metadata.emplace_back("grpc-status", std::to_string(kStatus));
  VerifyMetadataEqual(trailing_metadata,
                      recv_trailing_metadata.grpc_trailing_metadata);
  EXPECT_EQ(kMessage, recv_message.grpc_message->JoinIntoString());
}

TEST_F(BinderTransportTest, PerformAllOps) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();
  grpc_transport_stream_op_batch op{};
  grpc_transport_stream_op_batch_payload payload(nullptr);
  op.payload = &payload;

  const Metadata kSendInitialMetadata = kDefaultMetadata;
  const std::string kMethodRef = kDefaultMethodRef;
  MakeSendInitialMetadata send_initial_metadata(kSendInitialMetadata,
                                                kMethodRef, &op);

  const std::string kSendMessage = kDefaultMessage;
  MakeSendMessage send_message(kSendMessage, &op);

  // The wireformat guarantees that suffix metadata will always be empty.
  // TODO(waynetu): Check whether gRPC can internally add extra trailing
  // metadata.
  const Metadata kSendTrailingMetadata = {};
  MakeSendTrailingMetadata send_trailing_metadata(kSendTrailingMetadata, &op);

  MockGrpcClosure mock_on_complete;
  op.on_complete = mock_on_complete.GetGrpcClosure();

  // TODO(waynetu): Currently, we simply drop the prefix '/' from the :path
  // argument to obtain the method name. Update the test if this turns out to be
  // incorrect.
  EXPECT_CALL(GetWireWriter(),
              RpcCall(TransactionMatches(
                  kFlagPrefix | kFlagMessageData | kFlagSuffix,
                  kMethodRef.substr(1), kSendInitialMetadata, kSendMessage)));
  Expectation on_complete = EXPECT_CALL(mock_on_complete, Callback);

  // Recv callbacks can happen after the on_complete callback.
  MakeRecvInitialMetadata recv_initial_metadata(
      &op, /* call_before = */ &on_complete);
  MakeRecvMessage recv_message(&op, /* call_before = */ &on_complete);
  MakeRecvTrailingMetadata recv_trailing_metadata(
      &op, /* call_before = */ &on_complete);

  PerformStreamOp(gbs, &op);

  // Flush the execution context to force on_complete to run before recv
  // callbacks get scheduled.
  grpc_core::ExecCtx::Get()->Flush();

  auto* gbt = reinterpret_cast<grpc_binder_transport*>(transport_);
  const Metadata kRecvInitialMetadata =
      AppendMethodRef(kDefaultMetadata, kDefaultMethodRef);
  gbt->transport_stream_receiver->NotifyRecvInitialMetadata(
      gbs->tx_code, kRecvInitialMetadata);
  const std::string kRecvMessage = kDefaultMessage;
  gbt->transport_stream_receiver->NotifyRecvMessage(gbs->tx_code, kRecvMessage);
  const Metadata kRecvTrailingMetadata = kDefaultMetadata;
  constexpr int kStatus = 0x1234;
  gbt->transport_stream_receiver->NotifyRecvTrailingMetadata(
      gbs->tx_code, kRecvTrailingMetadata, kStatus);

  grpc_core::ExecCtx::Get()->Flush();
  recv_initial_metadata.notification.WaitForNotification();
  recv_message.notification.WaitForNotification();
  recv_trailing_metadata.notification.WaitForNotification();

  VerifyMetadataEqual(kRecvInitialMetadata,
                      recv_initial_metadata.grpc_initial_metadata);
  VerifyMetadataEqual(AppendStatus(kRecvTrailingMetadata, kStatus),
                      recv_trailing_metadata.grpc_trailing_metadata);

  EXPECT_EQ(kRecvMessage, recv_message.grpc_message->JoinIntoString());
}

TEST_F(BinderTransportTest, WireWriterRpcCallErrorPropagates) {
  grpc_core::ExecCtx exec_ctx;
  grpc_binder_stream* gbs = InitNewBinderStream();

  MockGrpcClosure mock_on_complete1;
  MockGrpcClosure mock_on_complete2;

  EXPECT_CALL(GetWireWriter(), RpcCall)
      .WillOnce(Return(absl::OkStatus()))
      .WillOnce(Return(absl::InternalError("WireWriter::RpcCall failed")));
  EXPECT_CALL(mock_on_complete1, Callback(absl::OkStatus()));
  EXPECT_CALL(mock_on_complete2,
              Callback(GrpcErrorMessageContains("WireWriter::RpcCall failed")));

  const Metadata kInitialMetadata = {};
  grpc_transport_stream_op_batch op1{};
  grpc_transport_stream_op_batch_payload payload1(nullptr);
  op1.payload = &payload1;
  MakeSendInitialMetadata send_initial_metadata1(kInitialMetadata, "", &op1);
  op1.on_complete = mock_on_complete1.GetGrpcClosure();

  grpc_transport_stream_op_batch op2{};
  grpc_transport_stream_op_batch_payload payload2(nullptr);
  op2.payload = &payload2;
  MakeSendInitialMetadata send_initial_metadata2(kInitialMetadata, "", &op2);
  op2.on_complete = mock_on_complete2.GetGrpcClosure();

  PerformStreamOp(gbs, &op1);
  PerformStreamOp(gbs, &op2);
  grpc_core::ExecCtx::Get()->Flush();
}

}  // namespace grpc_binder

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
