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
class CallTest extends \PHPUnit\Framework\TestCase
{
    public static $server;
    public static $port;
    private $channel;
    private $call;

    public static function setUpBeforeClass(): void
    {
        self::$server = new Grpc\Server([]);
        self::$port = self::$server->addHttp2Port('0.0.0.0:53000');
        self::$server->start();
    }

    public function setUp(): void
    {
        $this->channel = new Grpc\Channel('localhost:'.self::$port, [
            'force_new' => true,
        ]);
        $this->call = new Grpc\Call($this->channel,
                                    '/foo',
                                    Grpc\Timeval::infFuture());
    }

    public function tearDown(): void
    {
        $this->channel->close();
    }

    public function testConstructor()
    {
        $this->assertSame('Grpc\Call', get_class($this->call));
        $this->assertObjectHasAttribute('channel', $this->call);
    }

    public function testAddEmptyMetadata()
    {
        $batch = [
            Grpc\OP_SEND_INITIAL_METADATA => [],
        ];
        $result = $this->call->startBatch($batch);
        $this->assertTrue($result->send_metadata);
    }

    public function testAddSingleMetadata()
    {
        $batch = [
            Grpc\OP_SEND_INITIAL_METADATA => ['key' => ['value']],
        ];
        $result = $this->call->startBatch($batch);
        $this->assertTrue($result->send_metadata);
    }

    public function testAddMultiValueMetadata()
    {
        $batch = [
            Grpc\OP_SEND_INITIAL_METADATA => ['key' => ['value1', 'value2']],
        ];
        $result = $this->call->startBatch($batch);
        $this->assertTrue($result->send_metadata);
    }

    public function testAddSingleAndMultiValueMetadata()
    {
        $batch = [
            Grpc\OP_SEND_INITIAL_METADATA => ['key1' => ['value1'],
                                              'key2' => ['value2',
                                                         'value3', ], ],
        ];
        $result = $this->call->startBatch($batch);
        $this->assertTrue($result->send_metadata);
    }

    public function testAddMultiAndMultiValueMetadata() 
    {   
        $batch = [  
            Grpc\OP_SEND_INITIAL_METADATA => ['key1' => ['value1', 'value2'],   
                                              'key2' => ['value3', 'value4'],], 
        ];  
        $result = $this->call->startBatch($batch);  
        $this->assertTrue($result->send_metadata);  
    }

    public function testGetPeer()
    {
        $this->assertTrue(is_string($this->call->getPeer()));
    }

    public function testCancel()
    {
        $this->assertNull($this->call->cancel());
    }

    public function testInvalidStartBatchKey()
    {
        $this->expectException(\InvalidArgumentException::class);
        $batch = [
            'invalid' => ['key1' => 'value1'],
        ];
        $result = $this->call->startBatch($batch);
    }

    public function testInvalidMetadataStrKey()
    {
        $this->expectException(\InvalidArgumentException::class);
        $batch = [
            Grpc\OP_SEND_INITIAL_METADATA => ['Key' => ['value1', 'value2']],
        ];
        $result = $this->call->startBatch($batch);
    }

    public function testInvalidMetadataIntKey()
    {
        $this->expectException(\InvalidArgumentException::class);
        $batch = [
            Grpc\OP_SEND_INITIAL_METADATA => [1 => ['value1', 'value2']],
        ];
        $result = $this->call->startBatch($batch);
    }

    public function testInvalidMetadataInnerValue()
    {
        $this->expectException(\InvalidArgumentException::class);
        $batch = [
            Grpc\OP_SEND_INITIAL_METADATA => ['key1' => 'value1'],
        ];
        $result = $this->call->startBatch($batch);
    }

    public function testInvalidConstuctor()
    {
        $this->expectException(\InvalidArgumentException::class);
        $this->call = new Grpc\Call();
        $this->assertNull($this->call);
    }

    public function testInvalidConstuctor2()
    {
        $this->expectException(\InvalidArgumentException::class);
        $this->call = new Grpc\Call('hi', 'hi', 'hi');
        $this->assertNull($this->call);
    }

    public function testInvalidSetCredentials()
    {
        $this->expectException(\InvalidArgumentException::class);
        $this->call->setCredentials('hi');
    }

    public function testInvalidSetCredentials2()
    {
        $this->expectException(\InvalidArgumentException::class);
        $this->call->setCredentials([]);
    }
}
