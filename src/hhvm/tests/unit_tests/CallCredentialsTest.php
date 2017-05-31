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

class CallCredentialsTest extends PHPUnit_Framework_TestCase
{
    public function setUp()
    {
        $this->credentials = Grpc\ChannelCredentials::createSsl(
            file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
        $this->call_credentials = Grpc\CallCredentials::createFromPlugin(
            [$this, 'callbackFunc']);
        $this->credentials = Grpc\ChannelCredentials::createComposite(
            $this->credentials,
            $this->call_credentials
        );
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
            'credentials' => $this->credentials,
            ]
        );
    }

    public function tearDown()
    {
        unset($this->channel);
        unset($this->server);
    }

    public function callbackFunc($context)
    {
        $this->assertTrue(is_string($context->service_url));
        $this->assertTrue(is_string($context->method_name));

        return ['k1' => ['v1'], 'k2' => ['v2']];
    }

    public function testCreateFromPlugin()
    {
        $deadline = Grpc\Timeval::infFuture();
        $status_text = 'xyz';
        $call = new Grpc\Call($this->channel,
                              '/abc/dummy_method',
                              $deadline,
                              $this->host_override);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);

        $event = $this->server->requestCall();

        $this->assertTrue(is_array($event->metadata));
        $metadata = $event->metadata;
        $this->assertTrue(array_key_exists('k1', $metadata));
        $this->assertTrue(array_key_exists('k2', $metadata));
        $this->assertSame($metadata['k1'], ['v1']);
        $this->assertSame($metadata['k2'], ['v2']);

        $this->assertSame('/abc/dummy_method', $event->method);
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

    public function callbackFunc2($context)
    {
        return [];
    }

    public function testCreateComposite()
    {
        $call_credentials2 = Grpc\CallCredentials::createFromPlugin(
            [$this, 'callbackFunc2']);
        $call_credentials3 = Grpc\CallCredentials::createComposite(
            $this->call_credentials,
            $call_credentials2
        );
        $this->assertSame('Grpc\CallCredentials',
                          get_class($call_credentials3));
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateFromPluginInvalidParam()
    {
        $call_credentials = Grpc\CallCredentials::createFromPlugin(
            'callbackFunc'
        );
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateCompositeInvalidParam()
    {
        $call_credentials3 = Grpc\CallCredentials::createComposite(
            $this->call_credentials,
            $this->credentials
        );
    }
}
