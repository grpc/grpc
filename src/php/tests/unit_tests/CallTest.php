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
class CallTest extends PHPUnit_Framework_TestCase
{
    public static $server;
    public static $port;

    public static function setUpBeforeClass()
    {
        self::$server = new Grpc\Server([]);
        self::$port = self::$server->addHttp2Port('0.0.0.0:0');
    }

    public function setUp()
    {
        $this->channel = new Grpc\Channel('localhost:'.self::$port, []);
        $this->call = new Grpc\Call($this->channel,
                                    '/foo',
                                    Grpc\Timeval::infFuture());
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
                                              'key2' => ['value2', 'value3'], ],
        ];
        $result = $this->call->startBatch($batch);
        $this->assertTrue($result->send_metadata);
    }

    public function testGetPeer()
    {
        $this->assertTrue(is_string($this->call->getPeer()));
    }
}
