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

class ServerCallReader
{
    public function __construct($call, string $request_type)
    {
        $this->call_ = $call;
        $this->request_type_ = $request_type;
    }

    public function read()
    {
        $event = $this->call_->startBatch([
            OP_RECV_MESSAGE => true,
        ]);
        if ($event->message === null) {
            return null;
        }
        $data = new $this->request_type_;
        $data->mergeFromString($event->message);
        return $data;
    }

    private $call_;
    private $request_type_;
}
