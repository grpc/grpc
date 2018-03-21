<?php
// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
// Copyright 2017 gRPC authors.
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
class ProxyClientServiceClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param \Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
        parent::__construct($hostname, $opts, $channel);
    }

    /**
     * @param \Grpc\Testing\Void $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function GetConfig(\Grpc\Testing\Void $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.ProxyClientService/GetConfig',
        $argument,
        ['\Grpc\Testing\ClientConfig', 'decode'],
        $metadata, $options);
    }

    /**
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function ReportTime($metadata = [], $options = []) {
        return $this->_clientStreamRequest('/grpc.testing.ProxyClientService/ReportTime',
        ['\Grpc\Testing\Void','decode'],
        $metadata, $options);
    }

    /**
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function ReportHist($metadata = [], $options = []) {
        return $this->_clientStreamRequest('/grpc.testing.ProxyClientService/ReportHist',
        ['\Grpc\Testing\Void','decode'],
        $metadata, $options);
    }

}
