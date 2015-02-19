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
require_once realpath(dirname(__FILE__) . '/../autoload.php');

/**
 * Base class for generated client stubs. Stub methods are expected to call
 * _simpleRequest or _streamRequest and return the result.
 */
class BaseStub {

  private $channel;

  public function __construct($hostname, $opts) {
    $this->channel = new Channel($hostname, $opts);
  }

  /**
   * Close the communication channel associated with this stub
   */
  public function close() {
    $channel->close();
  }

  /* This class is intended to be subclassed by generated code, so all functions
     begin with "_" to avoid name collisions. */

  /**
   * Call a remote method that takes a single argument and has a single output
   *
   * @param string $method The name of the method to call
   * @param $argument The argument to the method
   * @param callable $deserialize A function that deserializes the response
   * @param array $metadata A metadata map to send to the server
   * @return SimpleSurfaceActiveCall The active call object
   */
  public function _simpleRequest($method,
                                 $argument,
                                 callable $deserialize,
                                 $metadata = array()) {
    return new SimpleSurfaceActiveCall($this->channel,
                                       $method,
                                       $deserialize,
                                       $argument,
                                       $metadata);
  }

  /**
   * Call a remote method that takes a stream of arguments and has a single
   * output
   *
   * @param string $method The name of the method to call
   * @param $arguments An array or Traversable of arguments to stream to the
   *     server
   * @param callable $deserialize A function that deserializes the response
   * @param array $metadata A metadata map to send to the server
   * @return ClientStreamingSurfaceActiveCall The active call object
   */
  public function _clientStreamRequest($method,
                                       $arguments,
                                       callable $deserialize,
                                       $metadata = array()) {
    return new ClientStreamingSurfaceActiveCall($this->channel,
                                                $method,
                                                $deserialize,
                                                $arguments,
                                                $metadata);
  }

  /**
   * Call a remote method that takes a single argument and returns a stream of
   * responses
   *
   * @param string $method The name of the method to call
   * @param $argument The argument to the method
   * @param callable $deserialize A function that deserializes the responses
   * @param array $metadata A metadata map to send to the server
   * @return ServerStreamingSurfaceActiveCall The active call object
   */
  public function _serverStreamRequest($method,
                                       $argument,
                                       callable $deserialize,
                                       $metadata = array()) {
    return new ServerStreamingSurfaceActiveCall($this->channel,
                                                $method,
                                                $deserialize,
                                                $argument,
                                                $metadata);
  }

  /**
   * Call a remote method with messages streaming in both directions
   *
   * @param string $method The name of the method to call
   * @param callable $deserialize A function that deserializes the responses
   * @param array $metadata A metadata map to send to the server
   * @return BidiStreamingSurfaceActiveCall The active call object
   */
  public function _bidiRequest($method,
                               callable $deserialize,
                               $metadata = array()) {
    return new BidiStreamingSurfaceActiveCall($this->channel,
                                              $method,
                                              $deserialize,
                                              $metadata);
  }
}
