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

abstract class AbstractCall
{
    protected $call;
    protected $deserialize;
    protected $metadata;

    /**
     * Create a new Call wrapper object.
     *
     * @param Channel  $channel     The channel to communicate on
     * @param string   $method      The method to call on the
     *                              remote server
     * @param callback $deserialize A callback function to deserialize
     *                              the response
     * @param array    $options     Call options (optional)
     */
    public function __construct(Channel $channel,
                                $method,
                                $deserialize,
                                $options = [])
    {
        if (isset($options['timeout']) &&
            is_numeric($timeout = $options['timeout'])) {
            $now = Timeval::now();
            $delta = new Timeval($timeout);
            $deadline = $now->add($delta);
        } else {
            $deadline = Timeval::infFuture();
        }
        $this->call = new Call($channel, $method, $deadline);
        $this->deserialize = $deserialize;
        $this->metadata = null;
        if (isset($options['call_credentials_callback']) &&
            is_callable($call_credentials_callback =
                        $options['call_credentials_callback'])) {
            $call_credentials = CallCredentials::createFromPlugin(
                $call_credentials_callback);
            $this->call->setCredentials($call_credentials);
        }
    }

    /**
     * @return The metadata sent by the server.
     */
    public function getMetadata()
    {
        return $this->metadata;
    }

    /**
     * @return string The URI of the endpoint.
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
     * Deserialize a response value to an object.
     *
     * @param string $value The binary value to deserialize
     *
     * @return The deserialized value
     */
    protected function deserializeResponse($value)
    {
        if ($value === null) {
            return;
        }

        return call_user_func($this->deserialize, $value);
    }

    /**
     * Set the CallCredentials for the underlying Call.
     *
     * @param CallCredentials $call_credentials The CallCredentials
     *                                          object
     */
    public function setCallCredentials($call_credentials)
    {
        $this->call->setCredentials($call_credentials);
    }
}
