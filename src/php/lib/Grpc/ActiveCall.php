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
 * Represents an active call that allows sending and recieving binary data
 */
class ActiveCall {
  private $completion_queue;
  private $call;
  private $flags;
  private $metadata;

  /**
   * Create a new active call.
   * @param Channel $channel The channel to communicate on
   * @param string $method The method to call on the remote server
   * @param array $metadata Metadata to send with the call, if applicable
   * @param long $flags Write flags to use with this call
   */
  public function __construct(Channel $channel,
                              $method,
                              $metadata = array(),
                              $flags = 0) {
    $this->completion_queue = new CompletionQueue();
    $this->call = new Call($channel, $method, Timeval::inf_future());
    $this->call->add_metadata($metadata, 0);
    $this->flags = $flags;

    // Invoke the call.
    $this->call->invoke($this->completion_queue,
                        CLIENT_METADATA_READ,
                        FINISHED, 0);
    $metadata_event = $this->completion_queue->pluck(CLIENT_METADATA_READ,
                                                     Timeval::inf_future());
    $this->metadata = $metadata_event->data;
  }

  /**
   * @return The metadata sent by the server.
   */
  public function getMetadata() {
    return $this->metadata;
  }

  /**
   * Cancels the call
   */
  public function cancel() {
    $this->call->cancel();
  }

  /**
   * Read a single message from the server.
   * @return The next message from the server, or null if there is none.
   */
  public function read() {
    $this->call->start_read(READ);
    $read_event = $this->completion_queue->pluck(READ, Timeval::inf_future());
    return $read_event->data;
  }

  /**
   * Write a single message to the server. This cannot be called after
   * writesDone is called.
   * @param ByteBuffer $data The data to write
   */
  public function write($data) {
    $this->call->start_write($data, WRITE_ACCEPTED, $this->flags);
    $this->completion_queue->pluck(WRITE_ACCEPTED, Timeval::inf_future());
  }

  /**
   * Indicate that no more writes will be sent.
   */
  public function writesDone() {
    $this->call->writes_done(FINISH_ACCEPTED);
    $this->completion_queue->pluck(FINISH_ACCEPTED, Timeval::inf_future());
  }

  /**
   * Wait for the server to send the status, and return it.
   * @return object The status object, with integer $code, string $details,
   *     and array $metadata members
   */
  public function getStatus() {
    $status_event = $this->completion_queue->pluck(FINISHED,
                                                   Timeval::inf_future());
    return $status_event->data;
  }
}
