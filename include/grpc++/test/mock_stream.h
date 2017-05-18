/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPCXX_TEST_MOCK_STREAM_H
#define GRPCXX_TEST_MOCK_STREAM_H

#include <stdint.h>

#include <gmock/gmock.h>
#include <grpc++/impl/codegen/call.h>
#include <grpc++/support/async_stream.h>
#include <grpc++/support/async_unary_call.h>
#include <grpc++/support/sync_stream.h>

namespace grpc {
namespace testing {

template <class R>
class MockClientReader : public ClientReaderInterface<R> {
 public:
  MockClientReader() = default;

  /// ClientStreamingInterface
  MOCK_METHOD0_T(Finish, Status());

  /// ReaderInterface
  MOCK_METHOD1_T(NextMessageSize, bool(uint32_t*));
  MOCK_METHOD1_T(Read, bool(R*));

  /// ClientReaderInterface
  MOCK_METHOD0_T(WaitForInitialMetadata, void());
};

template <class W>
class MockClientWriter : public ClientWriterInterface<W> {
 public:
  MockClientWriter() = default;

  /// ClientStreamingInterface
  MOCK_METHOD0_T(Finish, Status());

  /// WriterInterface
  MOCK_METHOD2_T(Write, bool(const W&, const WriteOptions));

  /// ClientWriterInterface
  MOCK_METHOD0_T(WritesDone, bool());
};

template <class W, class R>
class MockClientReaderWriter : public ClientReaderWriterInterface<W, R> {
 public:
  MockClientReaderWriter() = default;

  /// ClientStreamingInterface
  MOCK_METHOD0_T(Finish, Status());

  /// ReaderInterface
  MOCK_METHOD1_T(NextMessageSize, bool(uint32_t*));
  MOCK_METHOD1_T(Read, bool(R*));

  /// WriterInterface
  MOCK_METHOD2_T(Write, bool(const W&, const WriteOptions));

  /// ClientReaderWriterInterface
  MOCK_METHOD0_T(WaitForInitialMetadata, void());
  MOCK_METHOD0_T(WritesDone, bool());
};

/// TODO: We do not support mocking an async RPC for now.

template <class R>
class MockClientAsyncResponseReader
    : public ClientAsyncResponseReaderInterface<R> {
 public:
  MockClientAsyncResponseReader() = default;

  MOCK_METHOD1_T(ReadInitialMetadata, void(void*));
  MOCK_METHOD3_T(Finish, void(R*, Status*, void*));
};

template <class R>
class MockClientAsyncReader : public ClientAsyncReaderInterface<R> {
 public:
  MockClientAsyncReader() = default;

  /// ClientAsyncStreamingInterface
  MOCK_METHOD1_T(ReadInitialMetadata, void(void*));
  MOCK_METHOD2_T(Finish, void(Status*, void*));

  /// AsyncReaderInterface
  MOCK_METHOD2_T(Read, void(R*, void*));
};

template <class W>
class MockClientAsyncWriter : public ClientAsyncWriterInterface<W> {
 public:
  MockClientAsyncWriter() = default;

  /// ClientAsyncStreamingInterface
  MOCK_METHOD1_T(ReadInitialMetadata, void(void*));
  MOCK_METHOD2_T(Finish, void(Status*, void*));

  /// AsyncWriterInterface
  MOCK_METHOD2_T(Write, void(const W&, void*));

  /// ClientAsyncWriterInterface
  MOCK_METHOD1_T(WritesDone, void(void*));
};

template <class W, class R>
class MockClientAsyncReaderWriter
    : public ClientAsyncReaderWriterInterface<W, R> {
 public:
  MockClientAsyncReaderWriter() = default;

  /// ClientAsyncStreamingInterface
  MOCK_METHOD1_T(ReadInitialMetadata, void(void*));
  MOCK_METHOD2_T(Finish, void(Status*, void*));

  /// AsyncWriterInterface
  MOCK_METHOD2_T(Write, void(const W&, void*));

  /// AsyncReaderInterface
  MOCK_METHOD2_T(Read, void(R*, void*));

  /// ClientAsyncReaderWriterInterface
  MOCK_METHOD1_T(WritesDone, void(void*));
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPCXX_TEST_MOCK_STREAM_H
