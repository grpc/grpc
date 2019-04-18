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
namespace Grpc\Testing;

/**
 */
class WorkerServiceClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param \Grpc\Channel $channel (optional) re-use channel object
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
     * @param \Grpc\Testing\PBVoid $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function QuitWorker(\Grpc\Testing\PBVoid $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.WorkerService/QuitWorker',
        $argument,
        ['\Grpc\Testing\PBVoid', 'decode'],
        $metadata, $options);
    }

}
