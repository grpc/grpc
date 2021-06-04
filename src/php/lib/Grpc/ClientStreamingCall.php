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
 * Represents an active call that sends a stream of messages and then gets
 * a single response.
 */
class ClientStreamingCall extends AbstractCall
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
     * Start the call asynchronously.
     *
     * @param array $async_callbacks array of callbacks.
     *  'onMetadata' => function ($initial_metadata) { },
     *  'onData' => function ($response) { },
     *  'onStatus' => function ($status) { },
     * @param array $metadata Metadata to send with the call, if applicable
     *                        (optional)
     */
    public function startAsync(array $async_callbacks, array $metadata = [])
    {
        $this->async_callbacks_ = $async_callbacks;

        $this->call->startBatchAsync([
            OP_SEND_INITIAL_METADATA => $metadata,
        ], function ($error, $event = null) {
        });
    }

    /**
     * Write a single message to the server. This cannot be called after
     * wait is called.
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
        if ($this->async_callbacks_ === null) {
            $this->call->startBatch([
                OP_SEND_MESSAGE => $message_array,
            ]);
        } else {
            $writeFunction = function () use ($message_array) {
                $this->call->startBatchAsync([
                    OP_SEND_MESSAGE => $message_array,
                ], function ($error, $event = null) {
                    array_shift($this->async_write_queue_);
                    $nextWrite = reset($this->async_write_queue_);
                    if ($nextWrite) {
                        $nextWrite();
                    }
                });
            };

            array_push($this->async_write_queue_, $writeFunction);
            if (count($this->async_write_queue_) === 1) {
                $writeFunction();
            };
        }
    }

    /**
     * Indicate that no more writes will be sent.
     */
    public function writesDone()
    {
        if ($this->async_callbacks_ === null) {
            return;
        }

        $writeFunction = function () {
            $this->call->startBatchAsync([
                OP_SEND_CLOSE_FROM_CLIENT => true,
            ], function ($error, $event = null) {
            });

            // wait for response
            $this->call->startBatchAsync([
                OP_RECV_INITIAL_METADATA => true,
                OP_RECV_MESSAGE => true,
                OP_RECV_STATUS_ON_CLIENT => true,
            ], function ($error, $event = null) {
                if ($error) {
                    if (is_callable($this->async_callbacks_['onStatus'])) {
                        $this->async_callbacks_['onStatus'](
                            (object)Status::status(STATUS_UNKNOWN, $error)
                        );
                    }
                    return;
                }

                if (is_callable($this->async_callbacks_['onMetadata'])) {
                    $this->async_callbacks_['onMetadata']($event->metadata);
                }
                $this->metadata = $event->metadata;

                if ($event->message) {
                    $response = $this->_deserializeResponse($event->message);
                    if (is_callable($this->async_callbacks_['onData'])) {
                        $this->async_callbacks_['onData']($response);
                    }
                }

                if (is_callable($this->async_callbacks_['onStatus'])) {
                    $this->async_callbacks_['onStatus']($event->status);
                }
            });
        };

        array_push($this->async_write_queue_, $writeFunction);
        if (count($this->async_write_queue_) === 1) {
            $writeFunction();
        };
    }

    /**
     * Wait for the server to respond with data and a status.
     *
     * @return array [response data, status]
     */
    public function wait()
    {
        if ($this->async_callbacks_) {
            return;
        }
        $event = $this->call->startBatch([
            OP_SEND_CLOSE_FROM_CLIENT => true,
            OP_RECV_INITIAL_METADATA => true,
            OP_RECV_MESSAGE => true,
            OP_RECV_STATUS_ON_CLIENT => true,
        ]);
        $this->metadata = $event->metadata;

        $status = $event->status;
        $this->trailing_metadata = $status->metadata;

        return [$this->_deserializeResponse($event->message), $status];
    }

    private $async_callbacks_;
    private $async_write_queue_ = [];
}
