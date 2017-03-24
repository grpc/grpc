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

  class WorkerServiceClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
      parent::__construct($hostname, $opts, $channel);
    }

    /**
     * Start server with specified workload.
     * First request sent specifies the ServerConfig followed by ServerStatus
     * response. After that, a "Mark" can be sent anytime to request the latest
     * stats. Closing the stream will initiate shutdown of the test server
     * and once the shutdown has finished, the OK status is sent to terminate
     * this RPC.
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function RunServer($metadata = [], $options = []) {
      return $this->_bidiRequest('/grpc.testing.WorkerService/RunServer',
      ['\Grpc\Testing\ServerStatus','decode'],
      $metadata, $options);
    }

    /**
     * Start client with specified workload.
     * First request sent specifies the ClientConfig followed by ClientStatus
     * response. After that, a "Mark" can be sent anytime to request the latest
     * stats. Closing the stream will initiate shutdown of the test client
     * and once the shutdown has finished, the OK status is sent to terminate
     * this RPC.
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function RunClient($metadata = [], $options = []) {
      return $this->_bidiRequest('/grpc.testing.WorkerService/RunClient',
      ['\Grpc\Testing\ClientStatus','decode'],
      $metadata, $options);
    }

    /**
     * Just return the core count - unary call
     * @param \Grpc\Testing\CoreRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function CoreCount(\Grpc\Testing\CoreRequest $argument,
      $metadata = [], $options = []) {
      return $this->_simpleRequest('/grpc.testing.WorkerService/CoreCount',
      $argument,
      ['\Grpc\Testing\CoreResponse', 'decode'],
      $metadata, $options);
    }

    /**
     * Quit this worker
     * @param \Grpc\Testing\Void $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function QuitWorker(\Grpc\Testing\Void $argument,
      $metadata = [], $options = []) {
      return $this->_simpleRequest('/grpc.testing.WorkerService/QuitWorker',
      $argument,
      ['\Grpc\Testing\Void', 'decode'],
      $metadata, $options);
    }

  }

}
