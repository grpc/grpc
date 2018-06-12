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
namespace Grpc\Testing;

/**
 */
class EchoTestServiceClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param \Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
        parent::__construct($hostname, $opts, $channel);
    }

    /**
     * @param \Grpc\Testing\EchoRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function Echo(\Grpc\Testing\EchoRequest $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.EchoTestService/Echo',
        $argument,
        ['\Grpc\Testing\EchoResponse', 'decode'],
        $metadata, $options);
    }

    /**
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function RequestStream($metadata = [], $options = []) {
        return $this->_clientStreamRequest('/grpc.testing.EchoTestService/RequestStream',
        ['\Grpc\Testing\EchoResponse','decode'],
        $metadata, $options);
    }

    /**
     * @param \Grpc\Testing\EchoRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function ResponseStream(\Grpc\Testing\EchoRequest $argument,
      $metadata = [], $options = []) {
        return $this->_serverStreamRequest('/grpc.testing.EchoTestService/ResponseStream',
        $argument,
        ['\Grpc\Testing\EchoResponse', 'decode'],
        $metadata, $options);
    }

    /**
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function BidiStream($metadata = [], $options = []) {
        return $this->_bidiRequest('/grpc.testing.EchoTestService/BidiStream',
        ['\Grpc\Testing\EchoResponse','decode'],
        $metadata, $options);
    }

    /**
     * @param \Grpc\Testing\EchoRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function Unimplemented(\Grpc\Testing\EchoRequest $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.EchoTestService/Unimplemented',
        $argument,
        ['\Grpc\Testing\EchoResponse', 'decode'],
        $metadata, $options);
    }

}
