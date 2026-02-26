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

use Grpc\CallCredentials;
use Grpc\Channel;
use Grpc\ChannelCredentials;
use Grpc\Timeval;
use PHPUnit\Framework\TestCase;
use const Grpc\CHANNEL_CONNECTING;
use const Grpc\CHANNEL_IDLE;
use const Grpc\CHANNEL_TRANSIENT_FAILURE;

class ChannelTest extends TestCase
{
    private $channel;
    private $channel1;
    private $channel2;
    private $channel3;

    public function setUp(): void
    {
    }

    public function tearDown(): void
    {
        foreach ([$this->channel, $this->channel1, $this->channel2, $this->channel3] as $channel)
        if (!empty($channel)) {
            $channel->close();
        }
    }

    public function testInsecureCredentials()
    {
        $this->channel = new Channel('localhost:50000',
            ['credentials' => ChannelCredentials::createInsecure()]);
        $this->assertSame(Channel::class, get_class($this->channel));
    }

    public function testConstructorCreateSsl()
    {
        $channel = new Channel('localhost:50033', 
            ['credentials' => ChannelCredentials::createSsl()]);
        $this->assertNotNull($channel);
    }

    public function testCreateXdsWithSsl()
    {
        $xdsCreds = ChannelCredentials::createXds(
            ChannelCredentials::createSsl()
        );
        $this->assertNotNull($xdsCreds);
    }

    public function disabled_testCreateXdsWithInsecure() {
        $xdsCreds = ChannelCredentials::createXds(
            ChannelCredentials::createInsecure()
        );
        $this->assertNotNull($xdsCreds);
    }

    public function testCreateXdsWithNull() {
        $this->expectException(InvalidArgumentException::class);
        $xdsCreds = ChannelCredentials::createXds(null);
    }

    public function testCreateXdsWithInvalidType()
    {
        $expected = $this->logicalOr(
            // PHP8
            new \PHPUnit\Framework\Constraint\Exception(InvalidArgumentException::class),
            // PHP7
            new \PHPUnit\Framework\Constraint\Exception(\TypeError::class)
        );
        try {
            $xdsCreds = ChannelCredentials::createXds("invalid-type");
        } catch (\Throwable $exception) {
            $this->assertThat($exception, $expected);
            return;
        }
        $this->assertThat(null, $expected);
    }

    public function testGetConnectivityState()
    {
        $this->channel = new Channel('localhost:50001',
             ['credentials' => ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState();
        $this->assertEquals(0, $state);
    }

    public function testGetConnectivityStateWithInt()
    {
        $this->channel = new Channel('localhost:50002',
             ['credentials' => ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState(123);
        $this->assertEquals(0, $state);
    }

    public function testGetConnectivityStateWithString()
    {
        $this->channel = new Channel('localhost:50003',
             ['credentials' => ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState('hello');
        $this->assertEquals(0, $state);
    }

    public function testGetConnectivityStateWithBool()
    {
        $this->channel = new Channel('localhost:50004',
             ['credentials' => ChannelCredentials::createInsecure()]);
        $state = $this->channel->getConnectivityState(true);
        $this->assertEquals(0, $state);
    }

    public function testGetTarget()
    {
        $this->channel = new Channel('localhost:50005',
             ['credentials' => ChannelCredentials::createInsecure()]);
        $target = $this->channel->getTarget();
        $this->assertTrue(is_string($target));
    }

    public function testWatchConnectivityState()
    {
        $this->channel = new Channel('localhost:50006',
             ['credentials' => ChannelCredentials::createInsecure()]);
        $now = Timeval::now();
        $deadline = $now->add(new Timeval(100*1000));  // 100ms
        // we act as if 'CONNECTING'(=1) was the last state
        // we saw, so the default state of 'IDLE' should be delivered instantly
        $state = $this->channel->watchConnectivityState(1, $deadline);
        $this->assertTrue($state);
        unset($now);
        unset($deadline);
    }

    public function testClose()
    {
        $this->channel = new Channel('localhost:50007',
             ['credentials' => ChannelCredentials::createInsecure()]);
        $this->assertNotNull($this->channel);
        $this->channel->close();
    }

    public function testInvalidConstructorWithNull()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->channel = new Channel();
        $this->assertNull($this->channel);
    }

    public function testInvalidConstructorWith()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->channel = new Channel('localhost:50008', 'invalid');
        $this->assertNull($this->channel);
    }

    public function testInvalidCredentials()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->channel = new Channel('localhost:50009',
            ['credentials' => new Timeval(100)]);
    }

    public function testInvalidOptionsArray()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->channel = new Channel('localhost:50010',
            ['abc' => []]);
    }

    public function testInvalidGetConnectivityStateWithArray()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->channel = new Channel('localhost:50011',
            ['credentials' => ChannelCredentials::createInsecure()]);
        $this->channel->getConnectivityState([]);
    }

    public function testInvalidWatchConnectivityState()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->channel = new Channel('localhost:50012',
            ['credentials' => ChannelCredentials::createInsecure()]);
        $this->channel->watchConnectivityState([]);
    }

    public function testInvalidWatchConnectivityState2()
    {
        $this->expectException(InvalidArgumentException::class);
        $this->channel = new Channel('localhost:50013',
            ['credentials' => ChannelCredentials::createInsecure()]);
        $this->channel->watchConnectivityState(1, 'hi');
    }


    public function assertConnecting($state) {
      $this->assertTrue($state == CHANNEL_CONNECTING ||
                        $state == CHANNEL_TRANSIENT_FAILURE);
    }

    public function waitUntilNotIdle($channel) {
        for ($i = 0; $i < 10; $i++) {
            $now = Timeval::now();
            $deadline = $now->add(new Timeval(1000));
            if ($channel->watchConnectivityState(CHANNEL_IDLE,
                                                 $deadline)) {
                return true;
            }
        }
        $this->assertTrue(false);
    }

    public function testPersistentChannelSameHost()
    {
        $this->channel1 = new Channel('localhost:50014', [
            "grpc_target_persist_bound" => 3,
        ]);
        // the underlying grpc channel is the same by default
        // when connecting to the same host
        $this->channel2 = new Channel('localhost:50014', []);

        // both channels should be IDLE
        $state = $this->channel1->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

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
        $this->channel1 = new Channel('localhost:50015', [
            "grpc_target_persist_bound" => 3,
        ]);
        $this->channel2 = new Channel('localhost:50016', []);

        // both channels should be IDLE
        $state = $this->channel1->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        // channel1 should now be in the CONNECTING state
        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        // channel2 should still be in the IDLE state
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelSameArgs()
    {
        $this->channel1 = new Channel('localhost:50017', [
          "grpc_target_persist_bound" => 3,
          "abc" => "def",
          ]);
        $this->channel2 = new Channel('localhost:50017', ["abc" => "def"]);

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
        $this->channel1 = new Channel('localhost:50018', [
            "grpc_target_persist_bound" => 3,
          ]);
        $this->channel2 = new Channel('localhost:50018', ["abc" => "def"]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function persistentChannelSameChannelCredentialsProvider(): array
    {
        return [
            [
                ChannelCredentials::createSsl(),
                ChannelCredentials::createSsl(),
                50301,
            ],
            [
                ChannelCredentials::createSsl(
                    file_get_contents(dirname(__FILE__) . '/../data/ca.pem')
                ),
                ChannelCredentials::createSsl(
                    file_get_contents(dirname(__FILE__) . '/../data/ca.pem')
                ),
                50302,
            ],
            [
                ChannelCredentials::createInSecure(),
                ChannelCredentials::createInSecure(),
                50303,
            ],
            [
                ChannelCredentials::createXds(
                    ChannelCredentials::createSsl()
                ),
                ChannelCredentials::createXds(
                    ChannelCredentials::createSsl()
                ),
                50304,
            ],
            [
                ChannelCredentials::createXds(
                    ChannelCredentials::createSsl()
                ),
                ChannelCredentials::createXds(
                    ChannelCredentials::createSsl()
                ),
                50305,
            ],
            [
                ChannelCredentials::createXds(
                    ChannelCredentials::createSsl(
                        file_get_contents(dirname(__FILE__) . '/../data/ca.pem')
                    )
                ),
                ChannelCredentials::createXds(
                    ChannelCredentials::createSsl(
                        file_get_contents(dirname(__FILE__) . '/../data/ca.pem')
                    )
                ),
                50306,
            ],
            /*
            [
                \ChannelCredentials::createXds(
                    \ChannelCredentials::createInSecure()
                ),
                \ChannelCredentials::createXds(
                    \ChannelCredentials::createInSecure()
                ),
                50307,
            ],
            */
        ];
    }

    /**
     * @dataProvider persistentChannelSameChannelCredentialsProvider
     */
    public function testPersistentChannelSameChannelCredentials(
        $creds1,
        $creds2,
        $port
    ) {
        $this->channel1 = new Channel(
            'localhost:' . $port,
            [
                "credentials" => $creds1,
                "grpc_target_persist_bound" => 3,
            ]
        );
        $this->channel2 = new Channel(
            'localhost:' . $port,
            ["credentials" => $creds2]
        );

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

    public function persistentChannelDifferentChannelCredentialsProvider(): array
    {
        return [
            [
                ChannelCredentials::createSsl(),
                ChannelCredentials::createSsl(
                    file_get_contents(dirname(__FILE__) . '/../data/ca.pem')
                ),
                50351,
            ],
            [
                ChannelCredentials::createSsl(),
                ChannelCredentials::createInsecure(),
                50352,
            ],
            [
                ChannelCredentials::createXds(
                    ChannelCredentials::createSsl()
                ),
                ChannelCredentials::createXds(
                    ChannelCredentials::createSsl(
                        file_get_contents(dirname(__FILE__) . '/../data/ca.pem')
                    )
                ),
                50353,
            ],
            /*
            [
                \ChannelCredentials::createXds(
                    \ChannelCredentials::createSsl()
                ),
                \ChannelCredentials::createXds(
                    \ChannelCredentials::createInsecure()
                ),
                50354,
            ],
            [
                \ChannelCredentials::createInsecure(),
                \ChannelCredentials::createXds(
                    \ChannelCredentials::createInsecure()
                ),
                50355,
            ],
            */
            [
                ChannelCredentials::createSsl(),
                ChannelCredentials::createXds(
                    ChannelCredentials::createSsl()
                ),
                50356,
            ],
        ];
    }

    /**
     * @dataProvider persistentChannelDifferentChannelCredentialsProvider
     */
    public function testPersistentChannelDifferentChannelCredentials(
        $creds1,
        $creds2,
        $port
    ) {

        $this->channel1 = new Channel(
            'localhost:' . $port,
            [
                "credentials" => $creds1,
                "grpc_target_persist_bound" => 3,
            ]
        );
        $this->channel2 = new Channel(
            'localhost:' . $port,
            ["credentials" => $creds2]
        );

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelSharedChannelClose1()
    {
        // same underlying channel
        $this->channel1 = new Channel('localhost:50123', [
            "grpc_target_persist_bound" => 3,
        ]);
        $this->channel2 = new Channel('localhost:50123', []);

        // close channel1
        $this->channel1->close();

        // channel2 can still be use. We need to exclude the possible that
        // in testPersistentChannelSharedChannelClose2, the exception is thrown
        // by channel1.
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);
    }

    public function testPersistentChannelSharedChannelClose2()
    {
        $this->expectException(\RuntimeException::class);
        // same underlying channel
        $this->channel1 = new Channel('localhost:50223', [
            "grpc_target_persist_bound" => 3,
        ]);
        $this->channel2 = new Channel('localhost:50223', []);

        // close channel1
        $this->channel1->close();

        // channel2 can still be use
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        // channel 1 is closed
        $state = $this->channel1->getConnectivityState();
    }

    public function testPersistentChannelCreateAfterClose()
    {
        $this->channel1 = new Channel('localhost:50024', [
            "grpc_target_persist_bound" => 3,
        ]);

        $this->channel1->close();

        $this->channel2 = new Channel('localhost:50024', []);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        $this->channel2->close();
    }

    public function testPersistentChannelSharedMoreThanTwo()
    {
        $this->channel1 = new Channel('localhost:50025', [
            "grpc_target_persist_bound" => 3,
        ]);
        $this->channel2 = new Channel('localhost:50025', []);
        $this->channel3 = new Channel('localhost:50025', []);

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
        return ["k1" => "v1"];
    }

    public function testPersistentChannelWithCallCredentials()
    {
        $creds = ChannelCredentials::createSsl();
        $callCreds = CallCredentials::createFromPlugin(
            [$this, 'callbackFunc']);
        $credsWithCallCreds = ChannelCredentials::createComposite(
            $creds, $callCreds);

        // If a ChannelCredentials object is composed with a
        // CallCredentials object, the underlying grpc channel will
        // always be created new and NOT persisted.
        $this->channel1 = new Channel('localhost:50026',
                                           ["credentials" =>
                                            $credsWithCallCreds,
                                            "grpc_target_persist_bound" => 3,
                                            ]);
        $this->channel2 = new Channel('localhost:50026',
                                           ["credentials" =>
                                            $credsWithCallCreds]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelWithDifferentCallCredentials()
    {
        $callCreds1 = CallCredentials::createFromPlugin(
            [$this, 'callbackFunc']);
        $callCreds2 = CallCredentials::createFromPlugin(
            [$this, 'callbackFunc2']);

        $creds1 = ChannelCredentials::createSsl();
        $creds2 = ChannelCredentials::createComposite(
            $creds1, $callCreds1);
        $creds3 = ChannelCredentials::createComposite(
            $creds1, $callCreds2);

        // Similar to the test above, anytime a ChannelCredentials
        // object is composed with a CallCredentials object, the
        // underlying grpc channel will always be separate and not
        // persisted
        $this->channel1 = new Channel('localhost:50027',
                                           ["credentials" => $creds1,
                                            "grpc_target_persist_bound" => 3,
                                            ]);
        $this->channel2 = new Channel('localhost:50027',
                                           ["credentials" => $creds2]);
        $this->channel3 = new Channel('localhost:50027',
                                           ["credentials" => $creds3]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);
        $state = $this->channel3->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
        $this->channel3->close();
    }

    public function testPersistentChannelForceNew()
    {
        $this->channel1 = new Channel('localhost:50028', [
            "grpc_target_persist_bound" => 2,
        ]);
        // even though all the channel params are the same, channel2
        // has a new and different underlying channel
        $this->channel2 = new Channel('localhost:50028',
                                           ["force_new" => true]);

        // try to connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelForceNewOldChannelIdle1()
    {

        $this->channel1 = new Channel('localhost:50029', [
            "grpc_target_persist_bound" => 2,
        ]);
        $this->channel2 = new Channel('localhost:50029',
                                           ["force_new" => true]);
        // channel3 shares with channel1
        $this->channel3 = new Channel('localhost:50029', []);

        // try to connect on channel2
        $state = $this->channel2->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel2);
        $state = $this->channel1->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);
        $state = $this->channel2->getConnectivityState();
        $this->assertConnecting($state);
        $state = $this->channel3->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        $this->channel1->close();
        $this->channel2->close();
    }

    public function testPersistentChannelForceNewOldChannelIdle2()
    {

        $this->channel1 = new Channel('localhost:50032', [
            "grpc_target_persist_bound" => 2,
        ]);
        $this->channel2 = new Channel('localhost:50032', []);

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

        $this->channel1 = new Channel('localhost:50130', [
            "grpc_target_persist_bound" => 2,
        ]);
        $this->channel2 = new Channel('localhost:50130',
                                           ["force_new" => true]);
        // channel3 shares with channel1
        $this->channel3 = new Channel('localhost:50130', []);

        $this->channel1->close();

        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        // channel3 is still usable. We need to exclude the possibility that in
        // testPersistentChannelForceNewOldChannelClose2, the exception is thrown
        // by channel1 and channel2.
        $state = $this->channel3->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);
    }

    public function testPersistentChannelForceNewOldChannelClose2()
    {
        $this->expectException(\RuntimeException::class);
        $this->channel1 = new Channel('localhost:50230', [
            "grpc_target_persist_bound" => 2,
        ]);
        $this->channel2 = new Channel('localhost:50230',
          ["force_new" => true]);
        // channel3 shares with channel1
        $this->channel3 = new Channel('localhost:50230', []);

        $this->channel1->close();

        $state = $this->channel2->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        // channel3 is still usable
        $state = $this->channel3->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        // channel 1 is closed
        $this->channel1->getConnectivityState();
    }

    public function testPersistentChannelForceNewNewChannelClose()
    {

        $this->channel1 = new Channel('localhost:50031', [
            "grpc_target_persist_bound" => 2,
        ]);
        $this->channel2 = new Channel('localhost:50031',
                                           ["force_new" => true]);
        $this->channel3 = new Channel('localhost:50031', []);

        $this->channel2->close();

        $state = $this->channel1->getConnectivityState();
        $this->assertEquals(CHANNEL_IDLE, $state);

        // can still connect on channel1
        $state = $this->channel1->getConnectivityState(true);
        $this->waitUntilNotIdle($this->channel1);

        $state = $this->channel1->getConnectivityState();
        $this->assertConnecting($state);

        $this->channel1->close();
    }
}
