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
 * Represents an interceptor that intercept RPC invocations before call starts.
 * There is one proposal related to the argument $deserialize under the review.
 * The proposal link is https://github.com/grpc/proposal/pull/86.
 *
 * @template T of Message
 */
class Interceptor
{
    /**
     * @param string $method
     * @param mixed $argument
     * @param array{0: class-string<T>, 1: string} $deserialize
     * @param callable(string $method, T $argument, array{0: class-string<T>, 1: string} $deserialize, mixed[] $metadata, array<string, mixed> $options):UnaryCall $continuation
     * @param array<string, string[]> $metadata
     * @param array<string, mixed> $options
     *
     * @return UnaryCall
     */
    public function interceptUnaryUnary(
        $method,
        $argument,
        $deserialize,
        $continuation,
        array $metadata = [],
        array $options = []
    ) {
        return $continuation($method, $argument, $deserialize, $metadata, $options);
    }

    /**
     * @param string $method
     * @param array{0: class-string<T>, 1: string} $deserialize
     * @param callable(string $method, array{0: class-string<T>, 1: string} $deserialize, mixed[] $metadata, array<string, mixed> $options):ClientStreamingCall $continuation
     * @param array<string, string[]> $metadata
     * @param array<string, mixed> $options
     *
     * @return ClientStreamingCall
     */
    public function interceptStreamUnary(
        $method,
        $deserialize,
        $continuation,
        array $metadata = [],
        array $options = []
    ) {
        return $continuation($method, $deserialize, $metadata, $options);
    }

    /**
     * @param string $method
     * @param array{0: class-string<T>, 1: string} $deserialize
     * @param callable(string $method, T $argument, array{0: class-string<T>, 1: string} $deserialize, mixed[] $metadata, array<string, mixed> $options):ServerStreamingCall $continuation
     * @param array<string, string[]> $metadata
     * @param array<string, mixed> $options
     *
     * @return ServerStreamingCall
     */
    public function interceptUnaryStream(
        $method,
        $argument,
        $deserialize,
        $continuation,
        array $metadata = [],
        array $options = []
    ) {
        return $continuation($method, $argument, $deserialize, $metadata, $options);
    }

    /**
     * @param string $method
     * @param array{0: class-string<T>, 1: string} $deserialize
     * @param callable(string $method, array{0: class-string<T>, 1: string} $deserialize, mixed[] $metadata, array<string, mixed> $options):BidiStreamingCall $continuation
     * @param array<string, string[]> $metadata
     * @param array<string, mixed> $options
     *
     * @return BidiStreamingCall
     */
    public function interceptStreamStream(
        $method,
        $deserialize,
        $continuation,
        array $metadata = [],
        array $options = []
    ) {
        return $continuation($method, $deserialize, $metadata, $options);
    }

    /**
     * Intercept the methods with Channel
     *
     * @param InterceptorChannel $channel An already created Channel or InterceptorChannel object (optional)
     * @param Interceptor|Interceptor[] $interceptors interceptors to be added
     *
     * @return InterceptorChannel
     */
    public static function intercept($channel, $interceptors)
    {
        if (is_array($interceptors)) {
            for ($i = count($interceptors) - 1; $i >= 0; $i--) {
                $channel = new InterceptorChannel($channel, $interceptors[$i]);
            }
        } else {
            $channel =  new InterceptorChannel($channel, $interceptors);
        }
        return $channel;
    }
}

