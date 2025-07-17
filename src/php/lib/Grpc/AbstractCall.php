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

use Google\Protobuf\Internal\Message;

/**
 * Class AbstractCall.
 *
 * @package Grpc
 *
 * @template T of Message
 */
abstract class AbstractCall
{
    /**
     * @var Call
     */
    protected $call;
    /**
     * @var array{0: class-string<T>, 1: string}
     */
    protected $deserialize;
    /**
     * @var null|array<string, string[]>
     */
    protected $metadata;
    /**
     * @var null|string
     */
    protected $trailing_metadata;

    /**
     * Create a new Call wrapper object.
     *
     * @param Channel  $channel     The channel to communicate on
     * @param string   $method      The method to call on the
     *                              remote server
     * @param array{0: class-string<T>, 1: string} $deserialize A callback function to deserialize
     *                              the response
     * @param array<string, mixed>    $options     Call options (optional)
     */
    public function __construct(Channel $channel,
                                $method,
                                $deserialize,
                                array $options = [])
    {
        if (array_key_exists('timeout', $options) &&
            is_numeric($timeout = $options['timeout'])
        ) {
            $now = Timeval::now();
            $delta = new Timeval($timeout);
            $deadline = $now->add($delta);
        } else {
            $deadline = Timeval::infFuture();
        }
        $this->call = new Call($channel, $method, $deadline);
        $this->deserialize = $deserialize;
        $this->metadata = null;
        $this->trailing_metadata = null;
        if (array_key_exists('call_credentials_callback', $options) &&
            is_callable($call_credentials_callback =
                $options['call_credentials_callback'])
        ) {
            $call_credentials = CallCredentials::createFromPlugin(
                $call_credentials_callback
            );
            $this->call->setCredentials($call_credentials);
        }
    }

    /**
     * @return array<string, string[]> The metadata sent by the server
     */
    public function getMetadata()
    {
        return $this->metadata;
    }

    /**
     * @return string|null The trailing metadata sent by the server
     */
    public function getTrailingMetadata()
    {
        return $this->trailing_metadata;
    }

    /**
     * @return string The URI of the endpoint
     */
    public function getPeer()
    {
        return $this->call->getPeer();
    }

    /**
     * Cancels the call.
     */
    public function cancel()
    {
        $this->call->cancel();
    }

    /**
     * Serialize a message to the protobuf binary format.
     *
     * @param T $data The Protobuf message
     *
     * @return string The protobuf binary format
     */
    protected function _serializeMessage($data)
    {
        // Proto3 implementation
        return $data->serializeToString();
    }

    /**
     * Deserialize a response value to an object.
     *
     * @param string|null $value The binary value to deserialize
     *
     * @return T|null The deserialized value
     */
    protected function _deserializeResponse($value)
    {
        if ($value === null) {
            return null;
        }
        [$className] = $this->deserialize;
        /** @var T $obj */
        $obj = new $className();
        $obj->mergeFromString($value);
        return $obj;
    }

    /**
     * Set the CallCredentials for the underlying Call.
     *
     * @param CallCredentials $call_credentials The CallCredentials object
     */
    public function setCallCredentials($call_credentials)
    {
        $this->call->setCredentials($call_credentials);
    }
}
