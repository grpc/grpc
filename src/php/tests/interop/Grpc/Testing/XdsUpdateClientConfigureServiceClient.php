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
 * A service to dynamically update the configuration of an xDS test client.
 */
class XdsUpdateClientConfigureServiceClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param \Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
        parent::__construct($hostname, $opts, $channel);
    }

    /**
     * Update the tes client's configuration.
     * @param \Grpc\Testing\ClientConfigureRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\UnaryCall
     */
    public function Configure(\Grpc\Testing\ClientConfigureRequest $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.XdsUpdateClientConfigureService/Configure',
        $argument,
        ['\Grpc\Testing\ClientConfigureResponse', 'decode'],
        $metadata, $options);
    }

}
