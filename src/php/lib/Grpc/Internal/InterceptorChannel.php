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

namespace Grpc\Internal;

use Exception;
use Grpc\Channel;
use Grpc\Interceptor;
use Grpc\Timeval;

/**
 * This is a PRIVATE API and can change without notice.
 */
class InterceptorChannel extends Channel
{
    /**
     * @var Channel|InterceptorChannel
     */
    private $next = null;
    /**
     * @var Interceptor|Interceptor[] $interceptor
     */
    private $interceptor;

    /**
     * @param Channel|InterceptorChannel $channel An already created Channel
     * or InterceptorChannel object (optional)
     * @param Interceptor|Interceptor[]  $interceptor
     */
    public function __construct($channel, $interceptor)
    {
        if (!is_a($channel, 'Grpc\Channel') &&
            !is_a($channel, 'Grpc\Internal\InterceptorChannel')) {
            throw new Exception('The channel argument is not a Channel object '.
                'or an InterceptorChannel object created by '.
                'Interceptor::intercept($channel, Interceptor|Interceptor[] $interceptors)');
        }
        $this->interceptor = $interceptor;
        $this->next = $channel;
    }

    /**
     * @return Channel|InterceptorChannel|null
     */
    public function getNext()
    {
        return $this->next;
    }

    /**
     * @return Interceptor|Interceptor[]
     */
    public function getInterceptor()
    {
        return $this->interceptor;
    }

    /**
     * @return string
     */
    public function getTarget(): string
    {
        return $this->getNext()->getTarget();
    }

  /**
   * @param int $last_state
   * @param Timeval $deadline
   *
   * @return bool
   */
  public function watchConnectivityState(int $last_state, Timeval $deadline): bool
    {
        return $this->getNext()->watchConnectivityState($last_state, $deadline);
    }

  /**
   * @param bool $try_to_connect
   *
   * @return int
   */
    public function getConnectivityState(bool $try_to_connect = false): int
    {
        return $this->getNext()->getConnectivityState($try_to_connect);
    }

    public function close(): void
    {
        $this->getNext()->close();
    }
}
