/*
 *
 * Copyright 2017 gRPC authors.
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
