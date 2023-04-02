<?php
// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
// Copyright 2015-2016 gRPC authors.
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
// An integration test service that covers all the method signature permutations
// of unary/streaming requests/responses.
//
namespace Grpc\Testing;

/**
 * A simple service to test the various types of RPCs and experiment with
 * performance with various types of payload.
 */
class TestServiceStub {

    /**
     * One empty request followed by one empty response.
     * @param \Grpc\Testing\EmptyMessage $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Grpc\Testing\EmptyMessage for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function EmptyCall(
        \Grpc\Testing\EmptyMessage $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\EmptyMessage {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * One request followed by one response.
     * @param \Grpc\Testing\SimpleRequest $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Grpc\Testing\SimpleResponse for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function UnaryCall(
        \Grpc\Testing\SimpleRequest $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\SimpleResponse {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * One request followed by one response. Response has cache control
     * headers set such that a caching HTTP proxy (such as GFE) can
     * satisfy subsequent requests.
     * @param \Grpc\Testing\SimpleRequest $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Grpc\Testing\SimpleResponse for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function CacheableUnaryCall(
        \Grpc\Testing\SimpleRequest $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\SimpleResponse {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * One request followed by a sequence of responses (streamed download).
     * The server returns the payload with client desired type and sizes.
     * @param \Grpc\Testing\StreamingOutputCallRequest $request client request
     * @param \Grpc\ServerCallWriter $writer write response data of \Grpc\Testing\StreamingOutputCallResponse
     * @param \Grpc\ServerContext $context server request context
     * @return void
     */
    public function StreamingOutputCall(
        \Grpc\Testing\StreamingOutputCallRequest $request,
        \Grpc\ServerCallWriter $writer,
        \Grpc\ServerContext $context
    ): void {
        $context->setStatus(\Grpc\Status::unimplemented());
        $writer->finish();
    }

    /**
     * A sequence of requests followed by one response (streamed upload).
     * The server returns the aggregated size of client payload as the result.
     * @param \Grpc\ServerCallReader $reader read client request data of \Grpc\Testing\StreamingInputCallRequest
     * @param \Grpc\ServerContext $context server request context
     * @return \Grpc\Testing\StreamingInputCallResponse for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function StreamingInputCall(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\StreamingInputCallResponse {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * A sequence of requests with each request served by the server immediately.
     * As one request could lead to multiple responses, this interface
     * demonstrates the idea of full duplexing.
     * @param \Grpc\ServerCallReader $reader read client request data of \Grpc\Testing\StreamingOutputCallRequest
     * @param \Grpc\ServerCallWriter $writer write response data of \Grpc\Testing\StreamingOutputCallResponse
     * @param \Grpc\ServerContext $context server request context
     * @return void
     */
    public function FullDuplexCall(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerCallWriter $writer,
        \Grpc\ServerContext $context
    ): void {
        $context->setStatus(\Grpc\Status::unimplemented());
        $writer->finish();
    }

    /**
     * A sequence of requests followed by a sequence of responses.
     * The server buffers all the client requests and then serves them in order. A
     * stream of responses are returned to the client when the server starts with
     * first request.
     * @param \Grpc\ServerCallReader $reader read client request data of \Grpc\Testing\StreamingOutputCallRequest
     * @param \Grpc\ServerCallWriter $writer write response data of \Grpc\Testing\StreamingOutputCallResponse
     * @param \Grpc\ServerContext $context server request context
     * @return void
     */
    public function HalfDuplexCall(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerCallWriter $writer,
        \Grpc\ServerContext $context
    ): void {
        $context->setStatus(\Grpc\Status::unimplemented());
        $writer->finish();
    }

    /**
     * The test server will not implement this method. It will be used
     * to test the behavior when clients call unimplemented methods.
     * @param \Grpc\Testing\EmptyMessage $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Grpc\Testing\EmptyMessage for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function UnimplementedCall(
        \Grpc\Testing\EmptyMessage $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\EmptyMessage {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * Get the method descriptors of the service for server registration
     *
     * @return array of \Grpc\MethodDescriptor for the service methods
     */
    public final function getMethodDescriptors(): array
    {
        return [
            '/grpc.testing.TestService/EmptyCall' => new \Grpc\MethodDescriptor(
                $this,
                'EmptyCall',
                '\Grpc\Testing\EmptyMessage',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
            '/grpc.testing.TestService/UnaryCall' => new \Grpc\MethodDescriptor(
                $this,
                'UnaryCall',
                '\Grpc\Testing\SimpleRequest',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
            '/grpc.testing.TestService/CacheableUnaryCall' => new \Grpc\MethodDescriptor(
                $this,
                'CacheableUnaryCall',
                '\Grpc\Testing\SimpleRequest',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
            '/grpc.testing.TestService/StreamingOutputCall' => new \Grpc\MethodDescriptor(
                $this,
                'StreamingOutputCall',
                '\Grpc\Testing\StreamingOutputCallRequest',
                \Grpc\MethodDescriptor::SERVER_STREAMING_CALL
            ),
            '/grpc.testing.TestService/StreamingInputCall' => new \Grpc\MethodDescriptor(
                $this,
                'StreamingInputCall',
                '\Grpc\Testing\StreamingInputCallRequest',
                \Grpc\MethodDescriptor::CLIENT_STREAMING_CALL
            ),
            '/grpc.testing.TestService/FullDuplexCall' => new \Grpc\MethodDescriptor(
                $this,
                'FullDuplexCall',
                '\Grpc\Testing\StreamingOutputCallRequest',
                \Grpc\MethodDescriptor::BIDI_STREAMING_CALL
            ),
            '/grpc.testing.TestService/HalfDuplexCall' => new \Grpc\MethodDescriptor(
                $this,
                'HalfDuplexCall',
                '\Grpc\Testing\StreamingOutputCallRequest',
                \Grpc\MethodDescriptor::BIDI_STREAMING_CALL
            ),
            '/grpc.testing.TestService/UnimplementedCall' => new \Grpc\MethodDescriptor(
                $this,
                'UnimplementedCall',
                '\Grpc\Testing\EmptyMessage',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
        ];
    }

}
