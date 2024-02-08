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
 * Hook service. Used to keep Kubernetes from shutting the pod down.
 */
class HookServiceClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param \Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
        parent::__construct($hostname, $opts, $channel);
    }

    /**
     * Sends a request that will "hang" until the return status is set by a call
     * to a SetReturnStatus
     * @param \Grpc\Testing\EmptyMessage $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\UnaryCall
     */
    public function Hook(\Grpc\Testing\EmptyMessage $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.HookService/Hook',
        $argument,
        ['\Grpc\Testing\EmptyMessage', 'decode'],
        $metadata, $options);
    }

    /**
     * Sets a return status for pending and upcoming calls to Hook
     * @param \Grpc\Testing\SetReturnStatusRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\UnaryCall
     */
    public function SetReturnStatus(\Grpc\Testing\SetReturnStatusRequest $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.HookService/SetReturnStatus',
        $argument,
        ['\Grpc\Testing\EmptyMessage', 'decode'],
        $metadata, $options);
    }

    /**
     * Clears the return status. Incoming calls to Hook will "hang"
     * @param \Grpc\Testing\EmptyMessage $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\UnaryCall
     */
    public function ClearReturnStatus(\Grpc\Testing\EmptyMessage $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.HookService/ClearReturnStatus',
        $argument,
        ['\Grpc\Testing\EmptyMessage', 'decode'],
        $metadata, $options);
    }

}
