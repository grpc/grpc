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
