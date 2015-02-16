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
class SecureEndToEndTest extends PHPUnit_Framework_TestCase{
  public function setUp() {
    $this->client_queue = new Grpc\CompletionQueue();
    $this->server_queue = new Grpc\CompletionQueue();
    $credentials = Grpc\Credentials::createSsl(
        file_get_contents(dirname(__FILE__) . '/../data/ca.pem'));
    $server_credentials = Grpc\ServerCredentials::createSsl(
        null,
        file_get_contents(dirname(__FILE__) . '/../data/server1.key'),
        file_get_contents(dirname(__FILE__) . '/../data/server1.pem'));
    $this->server = new Grpc\Server($this->server_queue,
                                    ['credentials' => $server_credentials]);
    $port = $this->server->add_secure_http2_port('0.0.0.0:0');
    $this->channel = new Grpc\Channel(
        'localhost:' . $port,
        [
            'grpc.ssl_target_name_override' => 'foo.test.google.com',
            'credentials' => $credentials
         ]);
  }

  public function tearDown() {
    unset($this->channel);
    unset($this->server);
    unset($this->client_queue);
    unset($this->server_queue);
  }

  public function testSimpleRequestBody() {
    $this->server->start();
    $deadline = Grpc\Timeval::inf_future();
    $status_text = 'xyz';
    $call = new Grpc\Call($this->channel,
                          'dummy_method',
                          $deadline);
    $tag = 1;
    $call->invoke($this->client_queue, $tag, $tag);
    $server_tag = 2;

    $call->writes_done($tag);
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertSame(Grpc\OP_OK, $event->data);

    // check that a server rpc new was received
    $this->server->request_call($server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\SERVER_RPC_NEW, $event->type);
    $server_call = $event->call;
    $this->assertNotNull($server_call);
    $server_call->server_accept($this->server_queue, $server_tag);

    $server_call->server_end_initial_metadata();

    // the server sends the status
    $server_call->start_write_status(Grpc\STATUS_OK, $status_text, $server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertSame(Grpc\OP_OK, $event->data);

    // the client gets CLIENT_METADATA_READ
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\CLIENT_METADATA_READ, $event->type);

    // the client gets FINISHED
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISHED, $event->type);
    $status = $event->data;
    $this->assertSame(Grpc\STATUS_OK, $status->code);
    $this->assertSame($status_text, $status->details);

    // and the server gets FINISHED
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISHED, $event->type);
    $status = $event->data;

    unset($call);
    unset($server_call);
  }

  public function testClientServerFullRequestResponse() {
    $this->server->start();
    $deadline = Grpc\Timeval::inf_future();
    $req_text = 'client_server_full_request_response';
    $reply_text = 'reply:client_server_full_request_response';
    $status_text = 'status:client_server_full_response_text';

    $call = new Grpc\Call($this->channel,
                          'dummy_method',
                          $deadline);
    $tag = 1;
    $call->invoke($this->client_queue, $tag, $tag);

    $server_tag = 2;

    // the client writes
    $call->start_write($req_text, $tag);
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\WRITE_ACCEPTED, $event->type);

    // check that a server rpc new was received
    $this->server->request_call($server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\SERVER_RPC_NEW, $event->type);
    $server_call = $event->call;
    $this->assertNotNull($server_call);
    $server_call->server_accept($this->server_queue, $server_tag);

    $server_call->server_end_initial_metadata();

    // start the server read
    $server_call->start_read($server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\READ, $event->type);
    $this->assertSame($req_text, $event->data);

    // the server replies
    $server_call->start_write($reply_text, $server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\WRITE_ACCEPTED, $event->type);

    // the client reads the metadata
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\CLIENT_METADATA_READ, $event->type);

    // the client reads the reply
    $call->start_read($tag);
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\READ, $event->type);
    $this->assertSame($reply_text, $event->data);

    // the client sends writes done
    $call->writes_done($tag);
    $event = $this->client_queue->next($deadline);
    $this->assertSame(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertSame(Grpc\OP_OK, $event->data);

    // the server sends the status
    $server_call->start_write_status(GRPC\STATUS_OK, $status_text, $server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertSame(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertSame(Grpc\OP_OK, $event->data);

    // the client gets FINISHED
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISHED, $event->type);
    $status = $event->data;
    $this->assertSame(Grpc\STATUS_OK, $status->code);
    $this->assertSame($status_text, $status->details);

    // and the server gets FINISHED
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISHED, $event->type);

    unset($call);
    unset($server_call);
  }
}
