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

class ChannelTest extends PHPUnit_Framework_TestCase
{
    public function setUp()
    {
    }

    public function tearDown()
    {
        unset($this->channel);
    }

    public function testInsecureCredentials()
    {
        $this->channel = new Grpc\Channel(
            'localhost:0',
            [
                'credentials' => Grpc\ChannelCredentials::createInsecure(),
            ]
        );
        $this->assertSame('Grpc\Channel', get_class($this->channel));
    }

    public function testGetConnectivityState()
    {
        $this->channel = new Grpc\Channel('localhost:0',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState();
        $this->assertEquals(0, $state);
    }

    public function testGetConnectivityStateWithInt()
    {
        $this->channel = new Grpc\Channel('localhost:0',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState(123);
        $this->assertEquals(0, $state);
    }

    public function testGetConnectivityStateWithString()
    {
        $this->channel = new Grpc\Channel('localhost:0',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState('hello');
        $this->assertEquals(0, $state);
    }

    public function testGetConnectivityStateWithBool()
    {
        $this->channel = new Grpc\Channel('localhost:0',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState(true);
        $this->assertEquals(0, $state);
    }

    public function testGetTarget()
    {
        $this->channel = new Grpc\Channel('localhost:8888',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $target = $this->channel->getTarget();
        $this->assertTrue(is_string($target));
    }

    public function testWatchConnectivityState()
    {
        $this->channel = new Grpc\Channel('localhost:0',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $time = new Grpc\Timeval(1000);
        $state = $this->channel->watchConnectivityState(1, $time);
        $this->assertTrue($state);
        unset($time);
    }

    public function testClose()
    {
        $this->channel = new Grpc\Channel('localhost:0',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->assertNotNull($this->channel);
        $this->channel->close();
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidConstructorWithNull()
    {
        $this->channel = new Grpc\Channel();
        $this->assertNull($this->channel);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidConstructorWith()
    {
        $this->channel = new Grpc\Channel('localhost', 'invalid');
        $this->assertNull($this->channel);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidCredentials()
    {
        $this->channel = new Grpc\Channel(
            'localhost:0',
            [
                'credentials' => new Grpc\Timeval(100),
            ]
        );
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidOptionsArray()
    {
        $this->channel = new Grpc\Channel(
            'localhost:0',
            [
                'abc' => [],
            ]
        );
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidGetConnectivityStateWithArray()
    {
        $this->channel = new Grpc\Channel('localhost:0',
            ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->channel->getConnectivityState([]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidWatchConnectivityState()
    {
        $this->channel = new Grpc\Channel('localhost:0',
            ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->channel->watchConnectivityState([]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidWatchConnectivityState2()
    {
        $this->channel = new Grpc\Channel('localhost:0',
            ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->channel->watchConnectivityState(1, 'hi');
    }
}
