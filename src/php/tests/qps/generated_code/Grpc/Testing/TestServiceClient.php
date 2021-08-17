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
class TestServiceClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param \Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
        parent::__construct($hostname, $opts, $channel);
    }

    /**
     * One empty request followed by one empty response.
     * @param \Grpc\Testing\EmptyMessage $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\EmptyMessage
     */
    public function EmptyCall(\Grpc\Testing\EmptyMessage $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.TestService/EmptyCall',
        $argument,
        ['\Grpc\Testing\EmptyMessage', 'decode'],
        $metadata, $options);
    }

    /**
     * One request followed by one response.
     * @param \Grpc\Testing\SimpleRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\SimpleResponse
     */
    public function UnaryCall(\Grpc\Testing\SimpleRequest $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.TestService/UnaryCall',
        $argument,
        ['\Grpc\Testing\SimpleResponse', 'decode'],
        $metadata, $options);
    }

    /**
     * One request followed by one response. Response has cache control
     * headers set such that a caching HTTP proxy (such as GFE) can
     * satisfy subsequent requests.
     * @param \Grpc\Testing\SimpleRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\SimpleResponse
     */
    public function CacheableUnaryCall(\Grpc\Testing\SimpleRequest $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.TestService/CacheableUnaryCall',
        $argument,
        ['\Grpc\Testing\SimpleResponse', 'decode'],
        $metadata, $options);
    }

    /**
     * One request followed by a sequence of responses (streamed download).
     * The server returns the payload with client desired type and sizes.
     * @param \Grpc\Testing\StreamingOutputCallRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\StreamingOutputCallResponse
     */
    public function StreamingOutputCall(\Grpc\Testing\StreamingOutputCallRequest $argument,
      $metadata = [], $options = []) {
        return $this->_serverStreamRequest('/grpc.testing.TestService/StreamingOutputCall',
        $argument,
        ['\Grpc\Testing\StreamingOutputCallResponse', 'decode'],
        $metadata, $options);
    }

    /**
     * A sequence of requests followed by one response (streamed upload).
     * The server returns the aggregated size of client payload as the result.
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\StreamingInputCallResponse
     */
    public function StreamingInputCall($metadata = [], $options = []) {
        return $this->_clientStreamRequest('/grpc.testing.TestService/StreamingInputCall',
        ['\Grpc\Testing\StreamingInputCallResponse','decode'],
        $metadata, $options);
    }

    /**
     * A sequence of requests with each request served by the server immediately.
     * As one request could lead to multiple responses, this interface
     * demonstrates the idea of full duplexing.
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\StreamingOutputCallResponse
     */
    public function FullDuplexCall($metadata = [], $options = []) {
        return $this->_bidiRequest('/grpc.testing.TestService/FullDuplexCall',
        ['\Grpc\Testing\StreamingOutputCallResponse','decode'],
        $metadata, $options);
    }

    /**
     * A sequence of requests followed by a sequence of responses.
     * The server buffers all the client requests and then serves them in order. A
     * stream of responses are returned to the client when the server starts with
     * first request.
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\StreamingOutputCallResponse
     */
    public function HalfDuplexCall($metadata = [], $options = []) {
        return $this->_bidiRequest('/grpc.testing.TestService/HalfDuplexCall',
        ['\Grpc\Testing\StreamingOutputCallResponse','decode'],
        $metadata, $options);
    }

    /**
     * The test server will not implement this method. It will be used
     * to test the behavior when clients call unimplemented methods.
     * @param \Grpc\Testing\EmptyMessage $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\EmptyMessage
     */
    public function UnimplementedCall(\Grpc\Testing\EmptyMessage $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.TestService/UnimplementedCall',
        $argument,
        ['\Grpc\Testing\EmptyMessage', 'decode'],
        $metadata, $options);
    }

}
