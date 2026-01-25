<?php
/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
namespace Grpc;

use Google\Protobuf\Internal\Message;
use Grpc\Internal\InterceptorChannel;

/**
 * CallInvoker is used to pass the self defined channel into the stub,
 * while intercept each RPC with the channel accessible.
 *
 * @template T of Message
 */
interface CallInvoker
{
    /**
     * @param string $hostname
     * @param array<string, mixed> $opts
     *
     * @return Channel|InterceptorChannel
     */
    public function createChannelFactory($hostname, $opts);

    /**
     * @param Channel|InterceptorChannel $channel
     * @param string $method
     * @param array{0: class-string<T>, 1: string} $deserialize
     * @param array<string, mixed> $options
     *
     * @return UnaryCall
     */
    public function UnaryCall($channel, $method, $deserialize, $options);

    /**
     * @param Channel|InterceptorChannel $channel
     * @param string $method
     * @param array{0: class-string<T>, 1: string} $deserialize
     * @param array<string, mixed> $options
     *
     * @return ClientStreamingCall
     */
    public function ClientStreamingCall($channel, $method, $deserialize, $options);

    /**
     * @param Channel|InterceptorChannel $channel
     * @param string $method
     * @param array{0: class-string<T>, 1: string} $deserialize
     * @param array<string, mixed> $options
     *
     * @return ServerStreamingCall
     */
    public function ServerStreamingCall($channel, $method, $deserialize, $options);

    /**
     * @param Channel|InterceptorChannel $channel
     * @param string $method
     * @param array{0: class-string<T>, 1: string} $deserialize
     * @param array<string, mixed> $options
     *
     * @return BidiStreamingCall
     */
    public function BidiStreamingCall($channel, $method, $deserialize, $options);
}
