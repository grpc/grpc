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
require_once realpath(dirname(__FILE__).'/../../vendor/autoload.php');

// The following includes are needed when using protobuf 3.1.0
// and will suppress warnings when using protobuf 3.2.0+
@include_once dirname(__FILE__).'/math.pb.php';
@include_once dirname(__FILE__).'/math_grpc_pb.php';

abstract class AbstractGeneratedCodeTest extends \PHPUnit\Framework\TestCase
{
    /**
     * These tests require that a server exporting the math service must be
     * running on $GRPC_TEST_HOST.
     */
    protected static $client;

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

    public function testClose()
    {
        $this->expectException(\InvalidArgumentException::class);
        self::$client->close();
        $div_arg = new Math\DivArgs();
        $call = self::$client->Div($div_arg);
    }

    public function testInvalidMetadata()
    {
        $this->expectException(\InvalidArgumentException::class);
        $div_arg = new Math\DivArgs();
        $call = self::$client->Div($div_arg, [' ' => 'abc123']);
    }

    public function testMetadata()
    {
        $div_arg = new Math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $call = self::$client->Div($div_arg, ['somekey' => ['abc123']]);
        // $this->assertNotNull($call);
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testMetadataKey()
    {
        $div_arg = new Math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $call = self::$client->Div($div_arg, ['somekey_-1' => ['abc123']]);
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testMetadataKeyWithDot()
    {
        $div_arg = new Math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $call = self::$client->Div($div_arg, ['someKEY._-1' => ['abc123']]);
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testMetadataInvalidKey()
    {
        $this->expectException(\InvalidArgumentException::class);
        $div_arg = new Math\DivArgs();
        $call = self::$client->Div($div_arg, ['(somekey)' => ['abc123']]);
    }

    public function testGetCallMetadata()
    {
        $div_arg = new Math\DivArgs();
        $call = self::$client->Div($div_arg);
        $this->assertTrue(is_array($call->getMetadata()));
    }

    public function testTimeout()
    {
        $div_arg = new Math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $call = self::$client->Div($div_arg, [], ['timeout' => 1]);
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_DEADLINE_EXCEEDED, $status->code);
    }

    public function testCancel()
    {
        $div_arg = new Math\DivArgs();
        $call = self::$client->Div($div_arg);
        $call->cancel();
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_CANCELLED, $status->code);
    }

    public function testCallCredentialsCallback()
    {
        $div_arg = new Math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $call = self::$client->Div($div_arg, array(), array(
            'call_credentials_callback' => function ($context) {
                return array();
            },
        ));
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testInsecureChannelCallCredentialsCallback()
    {
        $div_arg = new Math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $client = new Math\MathClient(
            getenv('GRPC_TEST_INSECURE_HOST'), [
               'credentials' => Grpc\ChannelCredentials::createInsecure(),        
            ]);
        $call = $client->Div($div_arg, array(), array(
            'call_credentials_callback' => function ($context) {
                return array();
            },
        ));
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_UNAUTHENTICATED, $status->code);
    }

    public function testInvalidMethodName()
    {
        $this->expectException(\InvalidArgumentException::class);
        $invalid_client = new PhonyInvalidClient('host', [
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]);
        $div_arg = new Math\DivArgs();
        $invalid_client->InvalidUnaryCall($div_arg);
    }

    public function testMissingCredentials()
    {
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("The opts['credentials'] key is now required.");
        $invalid_client = new PhonyInvalidClient('host', [
        ]);
    }

    public function testPrimaryUserAgentString()
    {
        $invalid_client = new PhonyInvalidClient('host', [
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
            'grpc.primary_user_agent' => 'testUserAgent',
        ]);
        $this->assertTrue(TRUE); // to avoid no assert warning
    }

    public function testWriteFlags()
    {
        $div_arg = new Math\DivArgs();
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
        $fib_arg = new Math\FibArgs();
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
        $num = new Math\Num();
        $num->setNum(1);
        $call->write($num, ['flags' => Grpc\WRITE_NO_COMPRESS]);
        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
    }

    public function testWriteFlagsBidiStreaming()
    {
        $call = self::$client->DivMany();
        $div_arg = new Math\DivArgs();
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
        $div_arg = new Math\DivArgs();
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
        $fib_arg = new Math\FibArgs();
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
            $num = new Math\Num();
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
            $div_arg = new Math\DivArgs();
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

    public function testReuseCall()
    {
        $this->expectException(\LogicException::class);
        $this->expectExceptionMessage("start_batch was called incorrectly");
        $div_arg = new Math\DivArgs();
        $div_arg->setDividend(7);
        $div_arg->setDivisor(4);
        $call = self::$client->Div($div_arg, [], ['timeout' => 1000000]);

        list($response, $status) = $call->wait();
        $this->assertSame(\Grpc\STATUS_OK, $status->code);
        list($response, $status) = $call->wait();
    }
}

class PhonyInvalidClient extends \Grpc\BaseStub
{
    public function InvalidUnaryCall(\Math\DivArgs $argument,
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
