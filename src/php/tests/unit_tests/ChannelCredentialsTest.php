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

class ChannelCredentialsTest extends \PHPUnit\Framework\TestCase
{
    public function setUp(): void
    {
    }

    public function tearDown(): void
    {
    }

    public function testCreateSslWith3Null()
    {
        $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
                                                                  null);
        $this->assertNotNull($channel_credentials);
    }

    public function testCreateSslWith3NullString()
    {
        $channel_credentials = Grpc\ChannelCredentials::createSsl('', '', '');
        $this->assertNotNull($channel_credentials);
    }

    public function testCreateSslHashingIsolation()
    {
        $cred1 = Grpc\ChannelCredentials::createSsl('ca1', 'key1', 'cert1');
        $channel1 = new Grpc\Channel('localhost:1', ['credentials' => $cred1]);
        if (!method_exists($channel1, 'getChannelInfo')) {
            $this->markTestSkipped('GRPC_PHP_DEBUG not enabled');
        }
        $info1 = $channel1->getChannelInfo();

        // Change CA
        $cred2 = Grpc\ChannelCredentials::createSsl('ca2', 'key1', 'cert1');
        $channel2 = new Grpc\Channel('localhost:1', ['credentials' => $cred2]);
        $info2 = $channel2->getChannelInfo();

        // Change Key
        $cred3 = Grpc\ChannelCredentials::createSsl('ca1', 'key2', 'cert1');
        $channel3 = new Grpc\Channel('localhost:1', ['credentials' => $cred3]);
        $info3 = $channel3->getChannelInfo();

        // Change Cert
        $cred4 = Grpc\ChannelCredentials::createSsl('ca1', 'key1', 'cert2');
        $channel4 = new Grpc\Channel('localhost:1', ['credentials' => $cred4]);
        $info4 = $channel4->getChannelInfo();

        // All Same
        $cred5 = Grpc\ChannelCredentials::createSsl('ca1', 'key1', 'cert1');
        $channel5 = new Grpc\Channel('localhost:1', ['credentials' => $cred5]);
        $info5 = $channel5->getChannelInfo();

        $this->assertNotEquals($info1['key'], $info2['key']);
        $this->assertNotEquals($info1['key'], $info3['key']);
        $this->assertNotEquals($info1['key'], $info4['key']);
        $this->assertEquals($info1['key'], $info5['key']);

        $channel1->close();
        $channel2->close();
        $channel3->close();
        $channel4->close();
        $channel5->close();
    }

    public function testCreateInsecure()
    {
        $channel_credentials = Grpc\ChannelCredentials::createInsecure();
        $this->assertNull($channel_credentials);
    }

    public function testDefaultRootsPem()
    {
        Grpc\ChannelCredentials::setDefaultRootsPem("Pem-Content-Not-Verified");
        $this->assertTrue(Grpc\ChannelCredentials::isDefaultRootsPemSet());
        Grpc\ChannelCredentials::invalidateDefaultRootsPem();
        $this->assertFalse(Grpc\ChannelCredentials::isDefaultRootsPemSet());
        Grpc\ChannelCredentials::setDefaultRootsPem("Content-Not-Verified");
        $this->assertTrue(Grpc\ChannelCredentials::isDefaultRootsPemSet());
    }

    public function testInvalidCreateSsl()
    {
        $this->expectException(\InvalidArgumentException::class);
        $channel_credentials = Grpc\ChannelCredentials::createSsl([]);
    }

    public function testCreateCompositeWithDefault()
    {
        $cred1 = Grpc\ChannelCredentials::createDefault();
        $cred2 = Grpc\CallCredentials::createFromPlugin(function($context) { return []; });
        $channel_credentials = Grpc\ChannelCredentials::createComposite($cred1, $cred2);
        $this->assertNotNull($channel_credentials);
    }

    public function testInvalidCreateComposite()
    {
        $this->expectException(\InvalidArgumentException::class);
        $channel_credentials = Grpc\ChannelCredentials::createComposite(
            'something', 'something');
    }
}
