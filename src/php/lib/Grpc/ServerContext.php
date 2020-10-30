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

interface ServerContext
{
    public function clientMetadata();
    public function deadline();
    public function host();
    public function method();

    public function read();
    public function start($initialMetadata = []);
    public function write(
        \Google\Protobuf\Internal\Message $data,
        array $options = []
    );
    public function finish(
        \Google\Protobuf\Internal\Message $data = null,
        array $initialMetadata = null,
        $status = null
    );
}

class ServerContextImpl implements ServerContext
{
    public function __construct($event, $request_type)
    {
        $this->event = $event;
        var_dump($event);
        $this->request_type = $request_type;
    }

    public function clientMetadata()
    {
        return $this->event->metadata;
    }
    public function deadline()
    {
        return $this->event->absolute_deadline;
    }
    public function host()
    {
        return $this->event->host;
    }
    public function method()
    {
        return $this->event->method;
    }

    public function call()
    {
        return $this->event->call;
    }

    private $event;
    private $request_type;


    //

    public function read()
    {
        $event = $this->call()->startBatch([
            OP_RECV_MESSAGE => true,
        ]);
        if ($event->message === null) {
            return null;
        }
        $data = new $this->request_type;
        $data->mergeFromString($event->message);

        // throw new Exception("Unexpected error: fail to parse request");
        return $data;
    }

    //
    public function start($initialMetadata = [])
    {
        $batch = [
            OP_SEND_INITIAL_METADATA => $initialMetadata ?? [],
        ];
        $this->initialMetadataSent = true;
        $this->call()->startBatch($batch);
    }

    public function write(
        \Google\Protobuf\Internal\Message $data,
        array $options = []
    ) {
        $batch = [
            OP_SEND_MESSAGE => ['message' => $data->serializeToString()],
        ];
        if (!$this->initialMetadataSent) {
            $batch[OP_SEND_INITIAL_METADATA] = [];
            $this->initialMetadataSent = true;
        }
        $this->call()->startBatch($batch);
    }

    public function finish(
        \Google\Protobuf\Internal\Message $data = null,
        array $initialMetadata = null,
        $status = null
    ) {
        $batch = [
            OP_SEND_STATUS_FROM_SERVER => $status ?? Status::ok(),
            OP_RECV_CLOSE_ON_SERVER => true,
        ];
        if ($data) {
            $batch[OP_SEND_MESSAGE] = ['message' => $data->serializeToString()];
        }
        if (!$this->initialMetadataSent) {
            $batch[OP_SEND_INITIAL_METADATA] = $initialMetadata ?? [];
            $this->initialMetadataSent = true;
        }
        $this->call()->startBatch($batch);
    }

    private $initialMetadataSent = false;
}
