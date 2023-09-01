<?php
/*
 *
 * Copyright 2020 gRPC authors.
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

/**
 * This is an experimental and incomplete implementation of gRPC server
 * for PHP. APIs are _definitely_ going to be changed.
 *
 * DO NOT USE in production.
 */

class MethodDescriptor
{
    public function __construct(
        object $service,
        string $method_name,
        string $request_type,
        int $call_type
    ) {
        $this->service = $service;
        $this->method_name = $method_name;
        $this->request_type = $request_type;
        $this->call_type = $call_type;
    }

    public const UNARY_CALL = 0;
    public const SERVER_STREAMING_CALL = 1;
    public const CLIENT_STREAMING_CALL = 2;
    public const BIDI_STREAMING_CALL = 3;

    public $service;
    public $method_name;
    public $request_type;
    public $call_type;
}
