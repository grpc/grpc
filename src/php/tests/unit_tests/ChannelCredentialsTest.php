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

use Grpc\ChannelCredentials;
use PHPUnit\Framework\TestCase;

class ChannelCredentialsTest extends TestCase
{
    public function testCreateSslWith3Null()
    {
        $channel_credentials = ChannelCredentials::createSsl(null, null,
                                                                  null);
        $this->assertNotNull($channel_credentials);
    }

    public function testCreateSslWith3NullString()
    {
        $channel_credentials = ChannelCredentials::createSsl('', '', '');
        $this->assertNotNull($channel_credentials);
    }

    public function testCreateInsecure()
    {
        $channel_credentials = ChannelCredentials::createInsecure();
        $this->assertNull($channel_credentials);
    }

    public function testDefaultRootsPem()
    {
        ChannelCredentials::setDefaultRootsPem("Pem-Content-Not-Verified");
        $this->assertTrue(ChannelCredentials::isDefaultRootsPemSet());
        ChannelCredentials::invalidateDefaultRootsPem();
        $this->assertFalse(ChannelCredentials::isDefaultRootsPemSet());
        ChannelCredentials::setDefaultRootsPem("Content-Not-Verified");
        $this->assertTrue(ChannelCredentials::isDefaultRootsPemSet());
    }

    public function testInvalidCreateSsl()
    {
        $this->expectException(InvalidArgumentException::class);
        ChannelCredentials::createSsl([]);
    }

    public function testInvalidCreateComposite()
    {
        $this->expectException(InvalidArgumentException::class);
        ChannelCredentials::createComposite(
            'something', 'something');
    }
}
