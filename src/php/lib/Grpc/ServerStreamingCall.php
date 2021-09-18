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
 * stream of responses.
 */
class ServerStreamingCall extends AbstractCall
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
        if (array_key_exists('flags', $options)) {
            $message_array['flags'] = $options['flags'];
        }
        $this->call->startBatch([
            OP_SEND_INITIAL_METADATA => $metadata,
            OP_SEND_MESSAGE => $message_array,
            OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);
    }

    /**
     * Start the call asynchronously.
     *
     * @param array $async_callbacks array of callbacks.
     *  'onMetadata' => function ($initial_metadata) { },
     *  'onData' => function ($response) { },
     *  'onStatus' => function ($status) { },
     * @param mixed $data     The data to send
     * @param array $metadata Metadata to send with the call, if applicable
     *                        (optional)
     * @param array $options  An array of options, possible keys:
     *                        'flags' => a number (optional)
     */
    public function startAsync(
        array $async_callbacks,
        $data,
        array $metadata = [],
        array $options = []
    ) {
        $this->async_callbacks_ = $async_callbacks;

        $message_array = ['message' => $this->_serializeMessage($data)];
        if (array_key_exists('flags', $options)) {
            $message_array['flags'] = $options['flags'];
        }

        $this->call->startBatchAsync([
            OP_SEND_INITIAL_METADATA => $metadata,
            OP_SEND_MESSAGE => $message_array,
            OP_SEND_CLOSE_FROM_CLIENT => true,
        ], function ($error, $event = null) {
        });

        // receive initial metadata
        $this->call->startBatchAsync([
            OP_RECV_INITIAL_METADATA => true,
        ], function ($error, $event = null) {
            if (!$error) {
                if (is_callable($this->async_callbacks_['onMetadata'])) {
                    $this->async_callbacks_['onMetadata']($event->metadata);
                }
                $this->metadata = $event->metadata;
            }
        });

        // start async reading
        $receiveMessageCallback = function ($error, $event = null)
        use (&$receiveMessageCallback) {
            if ($error) {
                if (is_callable($this->async_callbacks_['onStatus'])) {
                    $this->async_callbacks_['onStatus'](
                        (object)Status::status(STATUS_UNKNOWN, $error)
                    );
                }
                return;
            }
            if ($event->message === null) {
                if (is_callable($this->async_callbacks_['onData'])) {
                    $this->async_callbacks_['onData'](null);
                }
                // server stream done, get status
                $this->call->startBatchAsync([
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
                    if (is_callable($this->async_callbacks_['onStatus'])) {
                        $this->async_callbacks_['onStatus']($event->status);
                    }
                });
                return;
            }
            $response = $this->_deserializeResponse($event->message);
            if (is_callable($this->async_callbacks_['onData'])) {
                $this->async_callbacks_['onData']($response);
            }

            // read next streaming messages
            $this->call->startBatchAsync([
                OP_RECV_MESSAGE => true,
            ], $receiveMessageCallback);
        };
        $this->call->startBatchAsync([
            OP_RECV_MESSAGE => true,
        ], $receiveMessageCallback);
    }

    /**
     * @return mixed An iterator of response values
     */
    public function responses()
    {
        if ($this->async_callbacks_) {
            return;
        }
        $batch = [OP_RECV_MESSAGE => true];
        if ($this->metadata === null) {
            $batch[OP_RECV_INITIAL_METADATA] = true;
        }
        $read_event = $this->call->startBatch($batch);
        if ($this->metadata === null) {
            $this->metadata = $read_event->metadata;
        }
        $response = $read_event->message;
        while ($response !== null) {
            yield $this->_deserializeResponse($response);
            $response = $this->call->startBatch([
                OP_RECV_MESSAGE => true,
            ])->message;
        }
    }

    /**
     * Wait for the server to send the status, and return it.
     *
     * @return \stdClass The status object, with integer $code, string
     *                   $details, and array $metadata members
     */
    public function getStatus()
    {
        if ($this->async_callbacks_) {
            return;
        }
        $status_event = $this->call->startBatch([
            OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $this->trailing_metadata = $status_event->status->metadata;

        return $status_event->status;
    }

    /**
     * @return mixed The metadata sent by the server
     */
    public function getMetadata()
    {
        if ($this->async_callbacks_) {
            return;
        }
        if ($this->metadata === null) {
            $event = $this->call->startBatch([OP_RECV_INITIAL_METADATA => true]);
            $this->metadata = $event->metadata;
        }
        return $this->metadata;
    }

    private $async_callbacks_;
}
