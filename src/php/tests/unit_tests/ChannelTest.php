<?php
/*
 *
 * Copyright 2018 gRPC authors.
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
        if (!empty($this->channel)) {
            $this->channel->close();
        }
    }

    public function testInsecureCredentials()
    {
        $this->channel = new Grpc\Channel('localhost:50000',
            ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->assertSame('Grpc\Channel', get_class($this->channel));
    }

    public function testGetConnectivityState()
    {
        $this->channel = new Grpc\Channel('localhost:50001',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState();
        $this->assertEquals(0, $state);
    }

    public function testGetConnectivityStateWithInt()
    {
        $this->channel = new Grpc\Channel('localhost:50002',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState(123);
        $this->assertEquals(0, $state);
    }

    public function testGetConnectivityStateWithString()
    {
        $this->channel = new Grpc\Channel('localhost:50003',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState('hello');
        $this->assertEquals(0, $state);
    }

    public function testGetConnectivityStateWithBool()
    {
        $this->channel = new Grpc\Channel('localhost:50004',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState(true);
        $this->assertEquals(0, $state);
    }

    public function testGetTarget()
    {
        $this->channel = new Grpc\Channel('localhost:50005',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $target = $this->channel->getTarget();
        $this->assertTrue(is_string($target));
    }

    public function testWatchConnectivityState()
    {
        $this->channel = new Grpc\Channel('localhost:50006',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $now = Grpc\Timeval::now();
        $deadline = $now->add(new Grpc\Timeval(100 * 1000));  // 100ms
        // we act as if 'CONNECTING'(=1) was the last state
        // we saw, so the default state of 'IDLE' should be delivered instantly
        $state = $this->channel->watchConnectivityState(1, $deadline);
        $this->assertTrue($state);
        unset($now);
        unset($deadline);
    }

    public function testClose()
    {
        $this->channel = new Grpc\Channel('localhost:50007',
             ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->assertNotNull($this->channel);
        $this->channel->close();
    }

    /**
     * @expectedException \InvalidArgumentException
     */
    public function testInvalidConstructorWithNull()
    {
        $this->channel = new Grpc\Channel();
        $this->assertNull($this->channel);
    }

    /**
     * @expectedException \InvalidArgumentException
     */
    public function testInvalidConstructorWith()
    {
        $this->channel = new Grpc\Channel('localhost:50008', 'invalid');
        $this->assertNull($this->channel);
    }

    /**
     * @expectedException \InvalidArgumentException
     */
    public function testInvalidCredentials()
    {
        $this->channel = new Grpc\Channel('localhost:50009',
            ['credentials' => new Grpc\Timeval(100)]);
    }

    /**
     * @expectedException \InvalidArgumentException
     */
    public function testInvalidOptionsArray()
    {
        $this->channel = new Grpc\Channel('localhost:50010',
            ['abc' => []]);
    }

    /**
     * @expectedException \InvalidArgumentException
     */
    public function testInvalidGetConnectivityStateWithArray()
    {
        $this->channel = new Grpc\Channel('localhost:50011',
            ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->channel->getConnectivityState([]);
    }

    /**
     * @expectedException \InvalidArgumentException
     */
    public function testInvalidWatchConnectivityState()
    {
        $this->channel = new Grpc\Channel('localhost:50012',
            ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->channel->watchConnectivityState([]);
    }

    /**
     * @expectedException \InvalidArgumentException
     */
    public function testInvalidWatchConnectivityState2()
    {
        $this->channel = new Grpc\Channel('localhost:50013',
            ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->channel->watchConnectivityState(1, 'hi');
    }

    public function assertConnecting($state)
    {
        $this->assertTrue($state == GRPC\CHANNEL_CONNECTING ||
                        $state == GRPC\CHANNEL_TRANSIENT_FAILURE);
    }

    public function waitUntilNotIdle($channel)
    {
        for ($i = 0; $i < 10; ++$i) {
            $now = Grpc\Timeval::now();
            $deadline = $now->add(new Grpc\Timeval(1000));
            if ($channel->watchConnectivityState(GRPC\CHANNEL_IDLE,
                                                 $deadline)) {
                return true;
            }
        }
        $this->assertTrue(false);
    }

    public function testPersistentChannelSameHost()
    {
        $this->channel1 = new Grpc\Channel('localhost:50014', [
            'grpc_target_persist_bound' => 3,
        ]);
        // the underlying grpc channel is the same by default
        // when connecting to the same host
        $this->channel2 = new Grpc\Channel('localhost:50014', []);

        // both channels should be IDLE
        $state = $this->channel1->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        // both channels should now be in the CONNECTING state
        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertConnecting($state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelDifferentHost()
    {
        // two different underlying channels because different hostname
        $this->channel1 = new Grpc\Channel('localhost:50015', [
            'grpc_target_persist_bound' => 3,
        ]);
        $this->channel2 = new Grpc\Channel('localhost:50016', []);

        // both channels should be IDLE
        $state = $this->channel1->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        // channel1 should now be in the CONNECTING state
        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        // channel2 should still be in the IDLE state
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelSameArgs()
    {
        $this->channel1 = new Grpc\Channel('localhost:50017', [
          'grpc_target_persist_bound' => 3,
          'abc' => 'def',
          ]);
        $this->channel2 = new Grpc\Channel('localhost:50017', ['abc' => 'def']);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertConnecting($state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelDifferentArgs()
    {
        $this->channel1 = new Grpc\Channel('localhost:50018', [
            'grpc_target_persist_bound' => 3,
          ]);
        $this->channel2 = new Grpc\Channel('localhost:50018', ['abc' => 'def']);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelSameChannelCredentials()
    {
        $creds1 = Grpc\ChannelCredentials::createSsl();
        $creds2 = Grpc\ChannelCredentials::createSsl();

        $this->channel1 = new Grpc\Channel('localhost:50019',
                                           ['credentials' => $creds1,
                                             'grpc_target_persist_bound' => 3,
                                             ]);
        $this->channel2 = new Grpc\Channel('localhost:50019',
                                           ['credentials' => $creds2]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertConnecting($state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelDifferentChannelCredentials()
    {
        $creds1 = Grpc\ChannelCredentials::createSsl();
        $creds2 = Grpc\ChannelCredentials::createSsl(
            file_get_contents(dirname(__FILE__).'/../data/ca.pem'));

        $this->channel1 = new Grpc\Channel('localhost:50020',
                                           ['credentials' => $creds1,
                                             'grpc_target_persist_bound' => 3,
                                             ]);
        $this->channel2 = new Grpc\Channel('localhost:50020',
                                           ['credentials' => $creds2]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelSameChannelCredentialsRootCerts()
    {
        $creds1 = Grpc\ChannelCredentials::createSsl(
            file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
        $creds2 = Grpc\ChannelCredentials::createSsl(
            file_get_contents(dirname(__FILE__).'/../data/ca.pem'));

        $this->channel1 = new Grpc\Channel('localhost:50021',
                                           ['credentials' => $creds1,
                                             'grpc_target_persist_bound' => 3,
                                             ]);
        $this->channel2 = new Grpc\Channel('localhost:50021',
                                           ['credentials' => $creds2]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertConnecting($state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelDifferentSecureChannelCredentials()
    {
        $creds1 = Grpc\ChannelCredentials::createSsl();
        $creds2 = Grpc\ChannelCredentials::createInsecure();

        $this->channel1 = new Grpc\Channel('localhost:50022',
                                           ['credentials' => $creds1,
                                             'grpc_target_persist_bound' => 3,
                                             ]);
        $this->channel2 = new Grpc\Channel('localhost:50022',
                                           ['credentials' => $creds2]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelSharedChannelClose1()
    {
        // same underlying channel
        $this->channel1 = new Grpc\Channel('localhost:50123', [
            'grpc_target_persist_bound' => 3,
        ]);
        $this->channel2 = new Grpc\Channel('localhost:50123', []);

        // close channel1
        $this->channel1->close();

        // channel2 can still be use. We need to exclude the possible that
        // in testPersistentChannelSharedChannelClose2, the exception is thrown
        // by channel1.
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);
    }

    /**
     * @expectedException \RuntimeException
     */
    public function testPersistentChannelSharedChannelClose2()
    {
        // same underlying channel
        $this->channel1 = new Grpc\Channel('localhost:50223', [
            'grpc_target_persist_bound' => 3,
        ]);
        $this->channel2 = new Grpc\Channel('localhost:50223', []);

        // close channel1
        $this->channel1->close();

        // channel2 can still be use
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        // channel 1 is closed
        $state = $this->channel1->getConnectivityState();
    }

    public function testPersistentChannelCreateAfterClose()
    {
        $this->channel1 = new Grpc\Channel('localhost:50024', [
            'grpc_target_persist_bound' => 3,
        ]);

        $this->channel1->close();

        $this->channel2 = new Grpc\Channel('localhost:50024', []);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        $this->channel2->close();
    }

    public function testPersistentChannelSharedMoreThanTwo()
    {
        $this->channel1 = new Grpc\Channel('localhost:50025', [
            'grpc_target_persist_bound' => 3,
        ]);
        $this->channel2 = new Grpc\Channel('localhost:50025', []);
        $this->channel3 = new Grpc\Channel('localhost:50025', []);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        // all 3 channels should be in CONNECTING state
        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel3->getConnectivityState();
        $this->assertConnecting($state);

        $this->channel1->close();
    }

    public function callbackFunc($context)
    {
        return [];
    }

    public function callbackFunc2($context)
    {
        return ['k1' => 'v1'];
    }

    public function testPersistentChannelWithCallCredentials()
    {
        $creds = Grpc\ChannelCredentials::createSsl();
        $callCreds = Grpc\CallCredentials::createFromPlugin(
            [$this, 'callbackFunc']);
        $credsWithCallCreds = Grpc\ChannelCredentials::createComposite(
            $creds, $callCreds);

        // If a ChannelCredentials object is composed with a
        // CallCredentials object, the underlying grpc channel will
        // always be created new and NOT persisted.
        $this->channel1 = new Grpc\Channel('localhost:50026',
                                           ['credentials' => $credsWithCallCreds,
                                            'grpc_target_persist_bound' => 3,
                                            ]);
        $this->channel2 = new Grpc\Channel('localhost:50026',
                                           ['credentials' => $credsWithCallCreds]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelWithDifferentCallCredentials()
    {
        $callCreds1 = Grpc\CallCredentials::createFromPlugin(
            [$this, 'callbackFunc']);
        $callCreds2 = Grpc\CallCredentials::createFromPlugin(
            [$this, 'callbackFunc2']);

        $creds1 = Grpc\ChannelCredentials::createSsl();
        $creds2 = Grpc\ChannelCredentials::createComposite(
            $creds1, $callCreds1);
        $creds3 = Grpc\ChannelCredentials::createComposite(
            $creds1, $callCreds2);

        // Similar to the test above, anytime a ChannelCredentials
        // object is composed with a CallCredentials object, the
        // underlying grpc channel will always be separate and not
        // persisted
        $this->channel1 = new Grpc\Channel('localhost:50027',
                                           ['credentials' => $creds1,
                                            'grpc_target_persist_bound' => 3,
                                            ]);
        $this->channel2 = new Grpc\Channel('localhost:50027',
                                           ['credentials' => $creds2]);
        $this->channel3 = new Grpc\Channel('localhost:50027',
                                           ['credentials' => $creds3]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);
        $state = $this->channel3->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
        $this->channel3->close();
    }

    public function testPersistentChannelForceNew()
    {
        $this->channel1 = new Grpc\Channel('localhost:50028', [
            'grpc_target_persist_bound' => 2,
        ]);
        // even though all the channel params are the same, channel2
        // has a new and different underlying channel
        $this->channel2 = new Grpc\Channel('localhost:50028',
                                           ['force_new' => true]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelForceNewOldChannelIdle1()
    {
        $this->channel1 = new Grpc\Channel('localhost:50029', [
            'grpc_target_persist_bound' => 2,
        ]);
        $this->channel2 = new Grpc\Channel('localhost:50029',
                                           ['force_new' => true]);
        // channel3 shares with channel1
        $this->channel3 = new Grpc\Channel('localhost:50029', []);

        // try to connect on channel2
        $state = $this->channel2->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel2);
        $state = $this->channel1->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);
        $state = $this->channel2->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel3->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelForceNewOldChannelIdle2()
    {
        $this->channel1 = new Grpc\Channel('localhost:50029', [
            'grpc_target_persist_bound' => 2,
        ]);
        $this->channel2 = new Grpc\Channel('localhost:50029', []);

        // try to connect on channel2
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel2);
        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertConnecting($state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelForceNewOldChannelClose1()
    {
        $this->channel1 = new Grpc\Channel('localhost:50130', [
            'grpc_target_persist_bound' => 2,
        ]);
        $this->channel2 = new Grpc\Channel('localhost:50130',
                                           ['force_new' => true]);
        // channel3 shares with channel1
        $this->channel3 = new Grpc\Channel('localhost:50130', []);

        $this->channel1->close();

        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        // channel3 is still usable. We need to exclude the possibility that in
        // testPersistentChannelForceNewOldChannelClose2, the exception is thrown
        // by channel1 and channel2.
        $state = $this->channel3->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);
    }

    /**
     * @expectedException \RuntimeException
     */
    public function testPersistentChannelForceNewOldChannelClose2()
    {
        $this->channel1 = new Grpc\Channel('localhost:50230', [
            'grpc_target_persist_bound' => 2,
        ]);
        $this->channel2 = new Grpc\Channel('localhost:50230',
          ['force_new' => true]);
        // channel3 shares with channel1
        $this->channel3 = new Grpc\Channel('localhost:50230', []);

        $this->channel1->close();

        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        // channel3 is still usable
        $state = $this->channel3->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        // channel 1 is closed
        $this->channel1->getConnectivityState();
    }

    public function testPersistentChannelForceNewNewChannelClose()
    {
        $this->channel1 = new Grpc\Channel('localhost:50031', [
            'grpc_target_persist_bound' => 2,
        ]);
        $this->channel2 = new Grpc\Channel('localhost:50031',
                                           ['force_new' => true]);
        $this->channel3 = new Grpc\Channel('localhost:50031', []);

        $this->channel2->close();

        $state = $this->channel1->getConnectivityState();
        $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

        // can still connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);

        $this->channel1->close();
    }
}
