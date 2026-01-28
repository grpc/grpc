<?php
/*
 *
 * Copyright 2015 gRPC authors.
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
 * Represents an active call that sends a single message and then gets a
 * single response.
 * 
 * @template T of \Google\Protobuf\Internal\Message
 */
class UnaryCall extends AbstractCall
{
    /**
     * Start the call.
     *
     * @param mixed $data     The data to send
     * @param array $metadata Metadata to send with the call, if applicable
     *                        (optional)
     * @param array $options  An array of options, possible keys:
     *                        'flags' => a number (optional)
     */
    public function start($data, array $metadata = [], array $options = [])
    {
        $message_array = ['message' => $this->_serializeMessage($data)];
        if (isset($options['flags'])) {
            $message_array['flags'] = $options['flags'];
        }
        $this->call->startBatch([
            OP_SEND_INITIAL_METADATA => $metadata,
            OP_SEND_MESSAGE => $message_array,
            OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);
    }

    /**
     * Wait for the server to respond with data and a status.
     *
     * @return array{0: T|null, 1: \stdClass} [response data, status]
     */
    public function wait()
    {
        $batch = [
            OP_RECV_MESSAGE => true,
            OP_RECV_STATUS_ON_CLIENT => true,
        ];
        if ($this->metadata === null) {
            $batch[OP_RECV_INITIAL_METADATA] = true;
        }
        $event = $this->call->startBatch($batch);
        if ($this->metadata === null) {
            $this->metadata = $event->metadata;
        }
        $status = $event->status;
        $this->trailing_metadata = $status->metadata;

        return [$this->_deserializeResponse($event->message), $status];
    }

    /**
     * @return mixed The metadata sent by the server
     */
    public function getMetadata()
    {
        if ($this->metadata === null) {
            $event = $this->call->startBatch([OP_RECV_INITIAL_METADATA => true]);
            $this->metadata = $event->metadata;
        }
        return $this->metadata;
    }
}
