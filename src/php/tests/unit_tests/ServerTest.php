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

class ServerTest extends PHPUnit_Framework_TestCase
{
    public function setUp()
    {
        $this->server = null;
    }

    public function tearDown()
    {
        unset($this->server);
    }

    public function testConstructorWithNull()
    {
        $this->server = new Grpc\Server();
        $this->assertNotNull($this->server);
    }

    public function testConstructorWithNullArray()
    {
        $this->server = new Grpc\Server([]);
        $this->assertNotNull($this->server);
    }

    public function testConstructorWithArray()
    {
        // key of array must be string
         $this->server = new Grpc\Server(['ip' => '127.0.0.1',
                                          'port' => '8080', ]);
        $this->assertNotNull($this->server);
    }

    public function testRequestCall()
    {
        $this->server = new Grpc\Server();
        $port = $this->server->addHttp2Port('0.0.0.0:0');
        $this->server->start();
        $channel = new Grpc\Channel('localhost:'.$port,
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);

        $deadline = Grpc\Timeval::infFuture();
        $call = new Grpc\Call($channel, 'dummy_method', $deadline);

        $event = $call->startBatch([Grpc\OP_SEND_INITIAL_METADATA => [],
                                    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
                                    ]);

        $c = $this->server->requestCall();
        $this->assertObjectHasAttribute('call', $c);
        $this->assertObjectHasAttribute('method', $c);
        $this->assertSame('dummy_method', $c->method);
        $this->assertObjectHasAttribute('host', $c);
        $this->assertTrue(is_string($c->host));
        $this->assertObjectHasAttribute('absolute_deadline', $c);
        $this->assertObjectHasAttribute('metadata', $c);

        unset($call);
        unset($channel);
    }

    private function createSslObj()
    {
        $server_credentials = Grpc\ServerCredentials::createSsl(
             null,
             file_get_contents(dirname(__FILE__).'/../data/server1.key'),
             file_get_contents(dirname(__FILE__).'/../data/server1.pem'));

        return $server_credentials;
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidConstructor()
    {
        $this->server = new Grpc\Server('invalid_host');
        $this->assertNull($this->server);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidAddHttp2Port()
    {
        $this->server = new Grpc\Server([]);
        $port = $this->server->addHttp2Port(['0.0.0.0:0']);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidAddSecureHttp2Port()
    {
        $this->server = new Grpc\Server([]);
        $port = $this->server->addSecureHttp2Port(['0.0.0.0:0']);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidAddSecureHttp2Port2()
    {
        $this->server = new Grpc\Server();
        $port = $this->server->addSecureHttp2Port('0.0.0.0:0');
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidAddSecureHttp2Port3()
    {
        $this->server = new Grpc\Server();
        $port = $this->server->addSecureHttp2Port('0.0.0.0:0', 'invalid');
    }
}
