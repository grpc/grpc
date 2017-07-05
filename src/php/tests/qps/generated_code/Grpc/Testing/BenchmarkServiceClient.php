<?php
// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
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
// An integration test service that covers all the method signature permutations
// of unary/streaming requests/responses.
namespace Grpc\Testing {

  class BenchmarkServiceClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
      parent::__construct($hostname, $opts, $channel);
    }

    /**
     * One request followed by one response.
     * The server returns the client payload as-is.
     * @param \Grpc\Testing\SimpleRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function UnaryCall(\Grpc\Testing\SimpleRequest $argument,
      $metadata = [], $options = []) {
      return $this->_simpleRequest('/grpc.testing.BenchmarkService/UnaryCall',
      $argument,
      ['\Grpc\Testing\SimpleResponse', 'decode'],
      $metadata, $options);
    }

    /**
     * One request followed by one response.
     * The server returns the client payload as-is.
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function StreamingCall($metadata = [], $options = []) {
      return $this->_bidiRequest('/grpc.testing.BenchmarkService/StreamingCall',
      ['\Grpc\Testing\SimpleResponse','decode'],
      $metadata, $options);
    }

  }

}
