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
 * Represents an active call that allows for sending and recieving messages
 * in streams in any order.
 */
class BidiStreamingCall extends AbstractCall
{
    /**
     * Start the call.
     *
     * @param array $metadata Metadata to send with the call, if applicable
     *                        (optional)
     */
    public function start(array $metadata = [])
    {
        $this->call->startBatch([
            OP_SEND_INITIAL_METADATA => $metadata,
        ]);
    }

    /**
     * Reads the next value from the server.
     *
     * @return mixed The next value from the server, or null if there is none
     */
    public function read()
    {
        $batch = [OP_RECV_MESSAGE => true];
        if ($this->metadata === null) {
            $batch[OP_RECV_INITIAL_METADATA] = true;
        }
        $read_event = $this->call->startBatch($batch);
        if ($this->metadata === null) {
            $this->metadata = $read_event->metadata;
        }

        return $this->_deserializeResponse($read_event->message);
    }

    /**
     * Write a single message to the server. This cannot be called after
     * writesDone is called.
     *
     * @param ByteBuffer $data    The data to write
     * @param array      $options An array of options, possible keys:
     *                            'flags' => a number (optional)
     */
    public function write($data, array $options = [])
    {
        $message_array = ['message' => $this->_serializeMessage($data)];
        if (array_key_exists('flags', $options)) {
            $message_array['flags'] = $options['flags'];
        }
        $this->call->startBatch([
            OP_SEND_MESSAGE => $message_array,
        ]);
    }

    /**
     * Indicate that no more writes will be sent.
     */
    public function writesDone()
    {
        $this->call->startBatch([
            OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);
    }

    /**
     * Wait for the server to send the status, and return it.
     *
     * @return \stdClass The status object, with integer $code, string
     *                   $details, and array $metadata members
     */
    public function getStatus()
    {
        $status_event = $this->call->startBatch([
            OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $this->trailing_metadata = $status_event->status->metadata;

        return $status_event->status;
    }
}
