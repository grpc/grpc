<?php
// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
