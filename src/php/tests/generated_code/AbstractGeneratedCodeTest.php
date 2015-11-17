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
require_once realpath(dirname(__FILE__).'/../../vendor/autoload.php');
require_once dirname(__FILE__).'/math.php';

abstract class AbstractGeneratedCodeTest extends PHPUnit_Framework_TestCase
{
    /**
     * These tests require that a server exporting the math service must be
     * running on $GRPC_TEST_HOST.
     */
    protected static $client;
    protected static $timeout;

    public function testWaitForNotReady()
    {
        $this->assertFalse(self::$client->waitForReady(1));
    }

    public function testWaitForReady()
    {
        $this->assertTrue(self::$client->waitForReady(250000));
    }

    public function testAlreadyReady()
    {
        $this->assertTrue(self::$client->waitForReady(250000));
        $this->assertTrue(self::$client->waitForReady(100));
    }

    public function testGetTarget()
    {
        $this->assertTrue(is_string(self::$client->getTarget()));
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testClose()
    {
        self::$client->close();
        $div_arg = new math\DivArgs();
        $call = self::$client->Div($div_arg);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidMetadata()
    {
        $div_arg = new math\DivArgs();
        $call = self::$client->Div($div_arg, [' ' => 'abc123']);
    }

    public function testGetCallMetadata()
    {
        $div_arg = new math\DivArgs();
        $call = self::$client->Div($div_arg);
        $this->assertTrue(is_array($call->getMetadata()));
    }

    public function testTimeout()
    {
        $div_arg = new math\DivArgs();
        $call = self::$client->Div($div_arg, ['timeout' => 100]);
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_DEADLINE_EXCEEDED, $status->code);
    }

    public function testCancel()
    {
        $div_arg = new math\DivArgs();
        $call = self::$client->Div($div_arg);
        $call->cancel();
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_CANCELLED, $status->code);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidMethodName()
    {
        $invalid_client = new DummyInvalidClient('host', []);
        $div_arg = new math\DivArgs();
        $invalid_client->InvalidUnaryCall($div_arg);
    }

    public function testWriteFlags()
    {
        $div_arg = new math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $call = self::$client->Div($div_arg, [],
                                   ['flags' => Grpc\WRITE_NO_COMPRESS]);
        $this->assertTrue(is_string($call->getPeer()));
        list($response, $status) = $call->wait();
        $this->assertSame(1, $response->getQuotient());
        $this->assertSame(3, $response->getRemainder());
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testWriteFlagsServerStreaming()
    {
        $fib_arg = new math\FibArgs();
        $fib_arg->setLimit(7);
        $call = self::$client->Fib($fib_arg, [],
                                   ['flags' => Grpc\WRITE_NO_COMPRESS]);
        $result_array = iterator_to_array($call->responses());
        $status = $call->getStatus();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testWriteFlagsClientStreaming()
    {
        $call = self::$client->Sum();
        $num = new math\Num();
        $num->setNum(1);
        $call->write($num, ['flags' => Grpc\WRITE_NO_COMPRESS]);
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testWriteFlagsBidiStreaming()
    {
        $call = self::$client->DivMany();
        $div_arg = new math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $call->write($div_arg, ['flags' => Grpc\WRITE_NO_COMPRESS]);
        $response = $call->read();
        $call->writesDone();
        $status = $call->getStatus();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testSimpleRequest()
    {
        $div_arg = new math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $call = self::$client->Div($div_arg);
        $this->assertTrue(is_string($call->getPeer()));
        list($response, $status) = $call->wait();
        $this->assertSame(1, $response->getQuotient());
        $this->assertSame(3, $response->getRemainder());
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testServerStreaming()
    {
        $fib_arg = new math\FibArgs();
        $fib_arg->setLimit(7);
        $call = self::$client->Fib($fib_arg);
        $this->assertTrue(is_string($call->getPeer()));
        $result_array = iterator_to_array($call->responses());
        $extract_num = function ($num) {
                         return $num->getNum();
                       };
        $values = array_map($extract_num, $result_array);
        $this->assertSame([1, 1, 2, 3, 5, 8, 13], $values);
        $status = $call->getStatus();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testClientStreaming()
    {
        $call = self::$client->Sum();
        $this->assertTrue(is_string($call->getPeer()));
        for ($i = 0; $i < 7; ++$i) {
            $num = new math\Num();
            $num->setNum($i);
            $call->write($num);
        }
        list($response, $status) = $call->wait();
        $this->assertSame(21, $response->getNum());
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testBidiStreaming()
    {
        $call = self::$client->DivMany();
        $this->assertTrue(is_string($call->getPeer()));
        for ($i = 0; $i < 7; ++$i) {
            $div_arg = new math\DivArgs();
            $div_arg->setDividend(2 * $i + 1);
            $div_arg->setDivisor(2);
            $call->write($div_arg);
            $response = $call->read();
            $this->assertSame($i, $response->getQuotient());
            $this->assertSame(1, $response->getRemainder());
        }
        $call->writesDone();
        $status = $call->getStatus();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }
}

class DummyInvalidClient extends \Grpc\BaseStub
{
    public function InvalidUnaryCall(\math\DivArgs $argument,
                                     $metadata = [],
                                     $options = [])
    {
        return $this->_simpleRequest('invalidMethodName',
                                     $argument,
                                     function () {},
                                     $metadata,
                                     $options);
    }
}
