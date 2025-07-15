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

use Grpc\Call;
use Grpc\ChannelCredentials;
use Grpc\Server;
use Grpc\Timeval;
use PHPUnit\Framework\TestCase;

class ServerTest extends TestCase
{
    /**
     * @var Server|null
     */
    private $server;

    public function setUp(): void
    {
        $this->server = null;
    }

    public function tearDown(): void
    {
        unset($this->server);
    }

    public function testConstructorWithNull()
    {
        $this->server = new Server();
        $this->assertNotNull($this->server);
    }

    public function testConstructorWithNullArray()
    {
        $this->server = new Server([]);
        $this->assertNotNull($this->server);
    }

    public function testConstructorWithArray()
    {
        // key of array must be string
         $this->server = new Server(['ip' => '127.0.0.1',
                                          'port' => '8080', ]);
        $this->assertNotNull($this->server);
    }

    public function testRequestCall()
    {
        $this->server = new Server();
        $port = $this->server->addHttp2Port('0.0.0.0:0');
        $this->server->start();
        $channel = new Grpc\Channel('localhost:'.$port,
             [
                 'force_new' => true,
                 'credentials' => ChannelCredentials::createInsecure()
             ]);

        $deadline = Timeval::infFuture();
        $call = new Call($channel, 'phony_method', $deadline);

        $event = $call->startBatch([Grpc\OP_SEND_INITIAL_METADATA => [],
                                    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
                                    ]);

        $c = $this->server->requestCall();
        $this->assertObjectHasProperty('call', $c);
        $this->assertObjectHasProperty('method', $c);
        $this->assertSame('phony_method', $c->method);
        $this->assertObjectHasProperty('host', $c);
        $this->assertTrue(is_string($c->host));
        $this->assertObjectHasProperty('absolute_deadline', $c);
        $this->assertObjectHasProperty('metadata', $c);

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

    public function testInvalidConstructor()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->server = new Server('invalid_host');
        $this->assertNull($this->server);
    }

    public function testInvalidConstructorWithNumKeyOfArray()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->server = new Server([10 => '127.0.0.1', 20 => '8080', ]);
        $this->assertNull($this->server);
    }

    public function testInvalidConstructorWithList()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->server = new Server(['127.0.0.1', '8080']);
        $this->assertNull($this->server);
    }

    public function testInvalidAddHttp2Port()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->server = new Grpc\Server([]);
        $this->server->addHttp2Port(['0.0.0.0:0']);
    }

    public function testInvalidAddSecureHttp2Port()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->server = new Grpc\Server([]);
        $this->server->addSecureHttp2Port(['0.0.0.0:0']);
    }

    public function testInvalidAddSecureHttp2Port2()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->server = new Grpc\Server();
        $this->server->addSecureHttp2Port('0.0.0.0:0');
    }

    public function testInvalidAddSecureHttp2Port3()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->server = new Grpc\Server();
        $this->server->addSecureHttp2Port('0.0.0.0:0', 'invalid');
    }
}
