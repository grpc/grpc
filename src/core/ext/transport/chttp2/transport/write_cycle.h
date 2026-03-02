//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_WRITE_CYCLE_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_WRITE_CYCLE_H

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <string>
#include <utility>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/ext/transport/chttp2/transport/write_size_policy.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "absl/container/inlined_vector.h"

namespace grpc_core {
namespace http2 {

// Tracks the number of bytes that can be written in the current write
// attempt.
class WriteQuota {
 public:
  explicit WriteQuota(size_t target_write_size)
      : target_write_size_(target_write_size) {}

  // WriteQuota is move-constructible but not copyable or assignable.
  WriteQuota(const WriteQuota&) = delete;
  WriteQuota& operator=(const WriteQuota&) = delete;
  WriteQuota(WriteQuota&&) = default;
  WriteQuota& operator=(WriteQuota&&) = delete;

  // Increments the bytes consumed for the current write attempt.
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void IncrementBytesConsumed(
      size_t bytes_consumed) {
    bytes_consumed_ += bytes_consumed;
  }

  // Returns the number of bytes remaining that can be written in the current
  // write attempt.
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION size_t GetWriteBytesRemaining() const {
    return (target_write_size_ > bytes_consumed_)
               ? target_write_size_ - bytes_consumed_
               : 0u;
  }

  // Returns the target write size for the current write attempt.
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION size_t GetTargetWriteSize() const {
    return target_write_size_;
  }

  std::string DebugString() const;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION size_t TestOnlyBytesConsumed() const {
    return bytes_consumed_;
  }

 private:
  const size_t target_write_size_;
  size_t bytes_consumed_ = 0;
};

// Tracks frames that need to be serialized for the current write attempt.
class WriteBufferTracker {
 public:
  static constexpr size_t kInlinedRegularFramesSize = 8;
  static constexpr size_t kInlinedUrgentFramesSize = 2;

  explicit WriteBufferTracker(bool& is_first_write, const bool is_client)
      : is_first_write_(is_first_write), is_client_(is_client) {}

  // WriteBufferTracker is move-constructible but not copyable or assignable.
  WriteBufferTracker(const WriteBufferTracker&) = delete;
  WriteBufferTracker& operator=(const WriteBufferTracker&) = delete;
  WriteBufferTracker(WriteBufferTracker&&) = default;
  WriteBufferTracker& operator=(WriteBufferTracker&&) = delete;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void AddRegularFrame(
      Http2Frame&& frame) {
    regular_frames_.emplace_back(std::forward<Http2Frame>(frame));
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void AddUrgentFrame(Http2Frame&& frame) {
    urgent_frames_.emplace_back(std::forward<Http2Frame>(frame));
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void ReserveRegularFrames(
      const size_t size) {
    regular_frames_.reserve(regular_frames_.size() + size);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool CanSerializeUrgentFrames() const {
    return !urgent_frames_.empty();
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool CanSerializeRegularFrames() const {
    return (!regular_frames_.empty() || is_first_write_);
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool HasFirstWriteHappened() const {
    return !is_first_write_;
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION size_t GetUrgentFrameCount() const {
    return urgent_frames_.size();
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION size_t GetRegularFrameCount() const {
    return regular_frames_.size();
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION
  absl::InlinedVector<Http2Frame, kInlinedRegularFramesSize>&
  TestOnlyRegularFrames() {
    return regular_frames_;
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION
  absl::InlinedVector<Http2Frame, kInlinedUrgentFramesSize>&
  TestOnlyUrgentFrames() {
    return urgent_frames_;
  }

  std::string DebugString() const;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Http2Frame* MutableLastRegularFrame() {
    return regular_frames_.empty() ? nullptr : &regular_frames_.back();
  }

  struct SerializeStats {
    bool& should_reset_ping_clock;
  };

  SliceBuffer SerializeRegularFrames(SerializeStats stats) {
    GRPC_DCHECK(CanSerializeRegularFrames());
    return SerializeFrames(regular_frames_, stats);
  }

  SliceBuffer SerializeUrgentFrames(SerializeStats stats) {
    GRPC_DCHECK(CanSerializeUrgentFrames());
    return SerializeFrames(urgent_frames_, stats);
  }

 private:
  template <typename FrameContainer>
  SliceBuffer SerializeFrames(FrameContainer& frames, SerializeStats stats) {
    SliceBuffer output_buf;
    if (GPR_UNLIKELY(is_first_write_)) {
      // https://www.rfc-editor.org/rfc/rfc9113.html#name-http-2-connection-preface
      // RFC9113:
      // The client and server each send a different connection preface.
      // Client: The connection preface starts with the string "PRI *
      // HTTP/2.0\r\n\r\nSM\r\n\r\n". This sequence MUST be followed by a
      // SETTINGS frame, which MAY be empty.
      // Server: The server connection preface consists of a potentially empty
      // SETTINGS frame that MUST be the first frame the server sends in the
      // HTTP/2 connection.
      if (is_client_) {
        output_buf.Append(
            Slice::FromCopiedString(GRPC_CHTTP2_CLIENT_CONNECT_STRING));
      }
      is_first_write_ = false;
    }
    SerializeReturn result =
        Serialize(absl::Span<Http2Frame>(frames), output_buf);
    frames.clear();
    stats.should_reset_ping_clock = result.should_reset_ping_clock;
    return output_buf;
  }

  // These frames are serialized and written to the endpoint in a single
  // endpoint write.
  absl::InlinedVector<Http2Frame, kInlinedRegularFramesSize> regular_frames_;
  // If there are urgent frames to be written, these frames are serialized
  // and written to the endpoint separately before the default frames are
  // written.
  absl::InlinedVector<Http2Frame, kInlinedUrgentFramesSize> urgent_frames_;
  bool& is_first_write_;
  const bool is_client_;
};

// Wrapper for WriteBufferTracker and WriteQuota to be used by the callers
// that only need to add frames to the write buffer.
class FrameSender {
 public:
  FrameSender(WriteBufferTracker& tracker, WriteQuota& quota)
      : tracker_(tracker), quota_(quota) {}

  // FrameSender is not copyable or assignable.
  FrameSender(FrameSender&&) = delete;
  FrameSender& operator=(FrameSender&&) = delete;
  FrameSender(const FrameSender&) = delete;
  FrameSender& operator=(const FrameSender&) = delete;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void AddRegularFrame(
      Http2Frame&& frame) {
    quota_.IncrementBytesConsumed(GetFrameMemoryUsage(frame));
    tracker_.AddRegularFrame(std::forward<Http2Frame>(frame));
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void AddUrgentFrame(Http2Frame&& frame) {
    // TODO(akshitpatel) [PH2][P5]: Maybe urgent frames should consume quota
    // too?
    tracker_.AddUrgentFrame(std::forward<Http2Frame>(frame));
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void ReserveRegularFrames(size_t size) {
    tracker_.ReserveRegularFrames(size);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Http2Frame* MutableLastRegularFrame() {
    return tracker_.MutableLastRegularFrame();
  }

 private:
  WriteBufferTracker& tracker_;
  WriteQuota& quota_;
};

// Per write cycle state.
class WriteCycle {
 public:
  WriteCycle(Chttp2WriteSizePolicy* write_size_policy, bool& is_first_write,
             const bool& is_client)
      : write_buffer_tracker_(is_first_write, is_client),
        write_quota_(write_size_policy->WriteTargetSize()),
        write_size_policy_(write_size_policy) {}

  // WriteCycle is move-constructible but not copyable or assignable.
  WriteCycle(const WriteCycle&) = delete;
  WriteCycle& operator=(const WriteCycle&) = delete;
  WriteCycle(WriteCycle&&) = default;
  WriteCycle& operator=(WriteCycle&&) = delete;

  using SerializeStats = WriteBufferTracker::SerializeStats;

  // Wrappers for Chttp2WriteSizePolicy
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void BeginWrite(
      const size_t bytes_to_write) {
    write_size_policy_->BeginWrite(bytes_to_write);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void EndWrite(bool success) {
    write_size_policy_->EndWrite(success);
  }

  // Wrappers for WriteQuota
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION size_t GetWriteBytesRemaining() const {
    return write_quota_.GetWriteBytesRemaining();
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION SliceBuffer
  SerializeRegularFrames(SerializeStats stats) {
    return write_buffer_tracker_.SerializeRegularFrames(stats);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION SliceBuffer
  SerializeUrgentFrames(SerializeStats stats) {
    return write_buffer_tracker_.SerializeUrgentFrames(stats);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool CanSerializeUrgentFrames() const {
    return write_buffer_tracker_.CanSerializeUrgentFrames();
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION size_t GetUrgentFrameCount() const {
    return write_buffer_tracker_.GetUrgentFrameCount();
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION size_t GetRegularFrameCount() const {
    return write_buffer_tracker_.GetRegularFrameCount();
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool CanSerializeRegularFrames() const {
    return write_buffer_tracker_.CanSerializeRegularFrames();
  }

  absl::InlinedVector<Http2Frame,
                      WriteBufferTracker::kInlinedRegularFramesSize>&
  TestOnlyRegularFrames();
  absl::InlinedVector<Http2Frame, WriteBufferTracker::kInlinedUrgentFramesSize>&
  TestOnlyUrgentFrames();

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION WriteBufferTracker&
  write_buffer_tracker() {
    return write_buffer_tracker_;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION FrameSender GetFrameSender() {
    return FrameSender(write_buffer_tracker_, write_quota_);
  }

  std::string DebugString() const;

 private:
  WriteBufferTracker write_buffer_tracker_;
  WriteQuota write_quota_;
  Chttp2WriteSizePolicy* write_size_policy_;
};

class TransportWriteContext {
 public:
  TransportWriteContext(const bool is_client) : is_client_(is_client) {}

  // TransportWriteContext cannot be copied, moved or assigned.
  TransportWriteContext(const TransportWriteContext&) = delete;
  TransportWriteContext& operator=(const TransportWriteContext&) = delete;
  TransportWriteContext(TransportWriteContext&&) = delete;
  TransportWriteContext& operator=(TransportWriteContext&&) = delete;

  void StartWriteCycle() {
    write_cycle_.emplace(&write_size_policy_, is_first_write_, is_client_);
  }

  void EndWriteCycle() { write_cycle_.reset(); }

  // Calls to this function MUST only be made between StartWriteCycle and
  // EndWriteCycle.
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION WriteCycle& GetWriteCycle() {
    return *write_cycle_;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool IsFirstWrite() const {
    return is_first_write_;
  }

  static PromiseEndpoint::WriteArgs GetWriteArgs(
      const Http2Settings& peer_settings);

  std::string DebugString() const;

 private:
  Chttp2WriteSizePolicy write_size_policy_;
  std::optional<WriteCycle> write_cycle_;
  bool is_first_write_ = true;
  const bool is_client_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_WRITE_CYCLE_H
