<?php
/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

namespace Grpc;

/**
 * Represents an active call that allows for sending and recieving messages in
 * streams in any order.
 */
class BidiStreamingCall extends AbstractCall
{
    /**
     * Start the call.
     *
     * @param array $metadata Metadata to send with the call, if applicable
     */
    public function start($metadata = [])
    {
        $this->call->startBatch([
            OP_SEND_INITIAL_METADATA => $metadata,
        ]);
    }

    /**
     * Reads the next value from the server.
     *
     * @return The next value from the server, or null if there is none
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

        return $this->deserializeResponse($read_event->message);
    }

    /**
     * Write a single message to the server. This cannot be called after
     * writesDone is called.
     *
     * @param ByteBuffer $data    The data to write
     * @param array      $options an array of options, possible keys:
     *                            'flags' => a number
     */
    public function write($data, $options = [])
    {
        $message_array = ['message' => $data->serialize()];
        if (isset($options['flags'])) {
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
     * @return object The status object, with integer $code, string $details,
     *                and array $metadata members
     */
    public function getStatus()
    {
        $status_event = $this->call->startBatch([
            OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        return $status_event->status;
    }
}
