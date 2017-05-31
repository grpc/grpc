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
class SecureEndToEndTest extends PHPUnit_Framework_TestCase
{
    public function setUp()
    {
        $credentials = Grpc\ChannelCredentials::createSsl(
            file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
        $server_credentials = Grpc\ServerCredentials::createSsl(
            null,
            file_get_contents(dirname(__FILE__).'/../data/server1.key'),
            file_get_contents(dirname(__FILE__).'/../data/server1.pem'));
        $this->server = new Grpc\Server();
        $this->port = $this->server->addSecureHttp2Port('0.0.0.0:0',
                                              $server_credentials);
        $this->server->start();
        $this->host_override = 'foo.test.google.fr';
        $this->channel = new Grpc\Channel(
            'localhost:'.$this->port,
            [
            'grpc.ssl_target_name_override' => $this->host_override,
            'grpc.default_authority' => $this->host_override,
            'credentials' => $credentials,
            ]
        );
    }

    public function tearDown()
    {
        unset($this->channel);
        unset($this->server);
    }

    public function testSimpleRequestBody()
    {
        $deadline = Grpc\Timeval::infFuture();
        $status_text = 'xyz';
        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline,
                              $this->host_override);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => $status_text,
            ],
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_status);
        $this->assertFalse($event->cancelled);

        $event = $call->startBatch([
            Grpc\OP_RECV_INITIAL_METADATA => true,
            Grpc\OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $this->assertSame([], $event->metadata);
        $status = $event->status;
        $this->assertSame([], $status->metadata);
        $this->assertSame(Grpc\STATUS_OK, $status->code);
        $this->assertSame($status_text, $status->details);

        unset($call);
        unset($server_call);
    }

    public function testMessageWriteFlags()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'message_write_flags_test';
        $status_text = 'xyz';
        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline,
                              $this->host_override);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text,
                                     'flags' => Grpc\WRITE_NO_COMPRESS, ],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => $status_text,
            ],
        ]);

        $event = $call->startBatch([
            Grpc\OP_RECV_INITIAL_METADATA => true,
            Grpc\OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $this->assertSame([], $event->metadata);
        $status = $event->status;
        $this->assertSame([], $status->metadata);
        $this->assertSame(Grpc\STATUS_OK, $status->code);
        $this->assertSame($status_text, $status->details);

        unset($call);
        unset($server_call);
    }

    public function testClientServerFullRequestResponse()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline,
                              $this->host_override);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);
        $this->assertTrue($event->send_message);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_MESSAGE => ['message' => $reply_text],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => $status_text,
            ],
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_status);
        $this->assertTrue($event->send_message);
        $this->assertFalse($event->cancelled);
        $this->assertSame($req_text, $event->message);

        $event = $call->startBatch([
            Grpc\OP_RECV_INITIAL_METADATA => true,
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $this->assertSame([], $event->metadata);
        $this->assertSame($reply_text, $event->message);
        $status = $event->status;
        $this->assertSame([], $status->metadata);
        $this->assertSame(Grpc\STATUS_OK, $status->code);
        $this->assertSame($status_text, $status->details);

        unset($call);
        unset($server_call);
    }
}
