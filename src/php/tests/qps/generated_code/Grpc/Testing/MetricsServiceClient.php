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
// Contains the definitions for a metrics service and the type of metrics
// exposed by the service.
//
// Currently, 'Gauge' (i.e a metric that represents the measured value of
// something at an instant of time) is the only metric type supported by the
// service.
namespace Grpc\Testing;

/**
 */
class MetricsServiceClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param \Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
        parent::__construct($hostname, $opts, $channel);
    }

    /**
     * Returns the values of all the gauges that are currently being maintained by
     * the service
     * @param \Grpc\Testing\EmptyMessage $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function GetAllGauges(\Grpc\Testing\EmptyMessage $argument,
      $metadata = [], $options = []) {
        return $this->_serverStreamRequest('/grpc.testing.MetricsService/GetAllGauges',
        $argument,
        ['\Grpc\Testing\GaugeResponse', 'decode'],
        $metadata, $options);
    }

    /**
     * Returns the value of one gauge
     * @param \Grpc\Testing\GaugeRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function GetGauge(\Grpc\Testing\GaugeRequest $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/grpc.testing.MetricsService/GetGauge',
        $argument,
        ['\Grpc\Testing\GaugeResponse', 'decode'],
        $metadata, $options);
    }

}
