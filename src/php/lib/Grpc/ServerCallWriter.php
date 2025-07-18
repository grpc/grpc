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

use Google\Protobuf\Internal\Message;

/**
 * This is an experimental and incomplete implementation of gRPC server
 * for PHP. APIs are _definitely_ going to be changed.
 *
 * DO NOT USE in production.
 */

/**
 * @template T of Message
 */
class ServerCallWriter
{
    /**
     * @param Call $call
     * @param ServerContext $serverContext
     */
    public function __construct($call, $serverContext)
    {
        $this->call_ = $call;
        $this->serverContext_ = $serverContext;
    }

    /**
     * @param T|null $data
     * @param array<string, mixed> $options
     *
     * @return void
     */
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

    /**
     * @param T $data
     * @param array<string, mixed> $options
     *
     * @return void
     */
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

    /**
     * @param T|null $data
     * @param array<string, mixed> $options
     *
     * @return void
     */
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

    /**
     * @param array<string|int, mixed> $batch
     * @param array<string, string[]> $initialMetadata
     */
    private function addSendInitialMetadataOpIfNotSent(
        array &$batch,
        ?array $initialMetadata = null
    ) {
        if (!$this->initialMetadataSent_) {
            $batch[OP_SEND_INITIAL_METADATA] = $initialMetadata ?? [];
            $this->initialMetadataSent_ = true;
        }
    }

    /**
     * @param array<string|int, mixed> $batch
     * @param T|null $data
     * @param array<string, mixed> $options
     *
     * @return void
     */
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

    /**
     * @var Call
     */
    private $call_;
    /**
     * @var bool
     */
    private $initialMetadataSent_ = false;
    /**
     * @var ServerContext
     */
    private $serverContext_;
}
