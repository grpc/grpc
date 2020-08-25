<?php
// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
// Copyright 2016 gRPC authors.
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
// File detached comment 1
//
// File detached comment 2
//
// File leading comment 1
namespace Grpc\Testing;

/**
 * ServiceA detached comment 1
 *
 * ServiceA detached comment 2
 *
 * ServiceA leading comment 1
 */
class ServiceAClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param \Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
        parent::__construct($hostname, $opts, $channel);
    }

    /**
     * MethodA1 leading comment 1
     * @param \Grpc\Testing\Request $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\Response
     */
    public function MethodA1(\Grpc\Testing\Request $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.ServiceA/MethodA1',
        $argument,
        ['\Grpc\Testing\Response', 'decode'],
        $metadata, $options);
    }

    /**
     * MethodA2 detached leading comment 1
     *
     * Method A2 leading comment 1
     * Method A2 leading comment 2
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\Response
     */
    public function MethodA2($metadata = [], $options = []) {
        return $this->_clientStreamRequest('/grpc.testing.ServiceA/MethodA2',
        ['\Grpc\Testing\Response','decode'],
        $metadata, $options);
    }

    /**
     * Method A3 leading comment 1
     * @param \Grpc\Testing\Request $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\Response
     */
    public function MethodA3(\Grpc\Testing\Request $argument,
      $metadata = [], $options = []) {
        return $this->_serverStreamRequest('/grpc.testing.ServiceA/MethodA3',
        $argument,
        ['\Grpc\Testing\Response', 'decode'],
        $metadata, $options);
    }

    /**
     * Method A4 leading comment 1
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\Testing\Response
     */
    public function MethodA4($metadata = [], $options = []) {
        return $this->_bidiRequest('/grpc.testing.ServiceA/MethodA4',
        ['\Grpc\Testing\Response','decode'],
        $metadata, $options);
    }

}
