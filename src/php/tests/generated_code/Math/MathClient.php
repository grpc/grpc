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
namespace Math;

/**
 */
class MathClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param \Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
        parent::__construct($hostname, $opts, $channel);
    }

    /**
     * Div divides DivArgs.dividend by DivArgs.divisor and returns the quotient
     * and remainder.
     * @param \Math\DivArgs $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\UnaryCall
     */
    public function Div(\Math\DivArgs $argument,
      $metadata = [], $options = []) {
        return $this->_simpleRequest('/math.Math/Div',
        $argument,
        ['\Math\DivReply', 'decode'],
        $metadata, $options);
    }

    /**
     * DivMany accepts an arbitrary number of division args from the client stream
     * and sends back the results in the reply stream.  The stream continues until
     * the client closes its end; the server does the same after sending all the
     * replies.  The stream ends immediately if either end aborts.
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\BidiStreamingCall
     */
    public function DivMany($metadata = [], $options = []) {
        return $this->_bidiRequest('/math.Math/DivMany',
        ['\Math\DivReply','decode'],
        $metadata, $options);
    }

    /**
     * Fib generates numbers in the Fibonacci sequence.  If FibArgs.limit > 0, Fib
     * generates up to limit numbers; otherwise it continues until the call is
     * canceled.  Unlike Fib above, Fib has no final FibReply.
     * @param \Math\FibArgs $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\ServerStreamingCall
     */
    public function Fib(\Math\FibArgs $argument,
      $metadata = [], $options = []) {
        return $this->_serverStreamRequest('/math.Math/Fib',
        $argument,
        ['\Math\Num', 'decode'],
        $metadata, $options);
    }

    /**
     * Sum sums a stream of numbers, returning the final result once the stream
     * is closed.
     * @param array $metadata metadata
     * @param array $options call options
     * @return \Grpc\ClientStreamingCall
     */
    public function Sum($metadata = [], $options = []) {
        return $this->_clientStreamRequest('/math.Math/Sum',
        ['\Math\Num','decode'],
        $metadata, $options);
    }

}
