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

class ServerCallWriter
{
    public function __construct($call, $serverContext)
    {
        $this->call_ = $call;
        $this->serverContext_ = $serverContext;
    }

    public function start(
        $data = null,
        array $options = []
    ) {
        $batch = [];
        $this->addSendInitialMetadataOpIfNotSent(
            $batch,
            $this->serverContext_->initialMetadata()
        );
        $this->addSendMessageOpIfHasData($batch, $data, $options);
        $this->call_->startBatch($batch);
    }

    public function write(
        $data,
        array $options = []
    ) {
        $batch = [];
        $this->addSendInitialMetadataOpIfNotSent(
            $batch,
            $this->serverContext_->initialMetadata()
        );
        $this->addSendMessageOpIfHasData($batch, $data, $options);
        $this->call_->startBatch($batch);
    }

    public function finish(
        $data = null,
        array $options = []
    ) {
        $batch = [
            OP_SEND_STATUS_FROM_SERVER =>
            $this->serverContext_->status() ?? Status::ok(),
            OP_RECV_CLOSE_ON_SERVER => true,
        ];
        $this->addSendInitialMetadataOpIfNotSent(
            $batch,
            $this->serverContext_->initialMetadata()
        );
        $this->addSendMessageOpIfHasData($batch, $data, $options);
        $this->call_->startBatch($batch);
    }

    ////////////////////////////

    private function addSendInitialMetadataOpIfNotSent(
        array &$batch,
        array $initialMetadata = null
    ) {
        if (!$this->initialMetadataSent_) {
            $batch[OP_SEND_INITIAL_METADATA] = $initialMetadata ?? [];
            $this->initialMetadataSent_ = true;
        }
    }

    private function addSendMessageOpIfHasData(
        array &$batch,
        $data = null,
        array $options = []
    ) {
        if ($data) {
            $message_array = ['message' => $data->serializeToString()];
            if (array_key_exists('flags', $options)) {
                $message_array['flags'] = $options['flags'];
            }
            $batch[OP_SEND_MESSAGE] = $message_array;
        }
    }

    private $call_;
    private $initialMetadataSent_ = false;
    private $serverContext_;
}
