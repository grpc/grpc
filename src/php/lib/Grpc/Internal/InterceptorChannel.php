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

/**
 * This is a PRIVATE API and can change without notice.
 */
class InterceptorChannel extends \Grpc\Channel
{
    private $next = null;
    private $interceptor;

    /**
     * @param Channel|InterceptorChannel $channel An already created Channel
     * or InterceptorChannel object (optional)
     * @param Interceptor  $interceptor
     */
    public function __construct($channel, $interceptor)
    {
        if (!is_a($channel, 'Grpc\Channel') &&
            !is_a($channel, 'Grpc\Internal\InterceptorChannel')) {
            throw new \Exception('The channel argument is not a Channel object '.
                'or an InterceptorChannel object created by '.
                'Interceptor::intercept($channel, Interceptor|Interceptor[] $interceptors)');
        }
        $this->interceptor = $interceptor;
        $this->next = $channel;
    }

    public function getNext()
    {
        return $this->next;
    }

    public function getInterceptor()
    {
        return $this->interceptor;
    }

    public function getTarget()
    {
        return $this->getNext()->getTarget();
    }

    public function watchConnectivityState($new_state, $deadline)
    {
        return $this->getNext()->watchConnectivityState($new_state, $deadline);
    }

    public function getConnectivityState($try_to_connect = false)
    {
        return $this->getNext()->getConnectivityState($try_to_connect);
    }

    public function close()
    {
        return $this->getNext()->close();
    }
}
