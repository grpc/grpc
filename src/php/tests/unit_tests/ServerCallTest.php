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

use Grpc\ServerCallReader;
use Grpc\ServerCallWriter;
use Grpc\ServerContext;
use Grpc\Status;
use PHPUnit\Framework\TestCase;
use const Grpc\OP_RECV_CLOSE_ON_SERVER;
use const Grpc\OP_RECV_MESSAGE;
use const Grpc\OP_SEND_INITIAL_METADATA;
use const Grpc\OP_SEND_MESSAGE;
use const Grpc\OP_SEND_STATUS_FROM_SERVER;
use const Grpc\STATUS_INVALID_ARGUMENT;

class StartBatchEvent
{
    public function __construct(string $message)
    {
        $this->message = $message;
    }
    public $message;
}

class StringValue
{
    public function setValue(string $value)
    {
        $this->value = $value;
    }

    public function getValue(): string
    {
        return $this->value;
    }

    public function serializeToString(): string
    {
        return $this->value;
    }

    public function mergeFromString(string $value)
    {
        $this->value = $value;
    }

    private $value = '';
}

class ServerCallTest extends TestCase
{
    private $mockCall;
    private $serverContext;

    public function setUp(): void
    {
        $this->mockCall = $this->getMockBuilder(stdClass::class)
            ->addMethods(['startBatch'])
            ->getMock();
        $this->serverContext = new ServerContext($this->mockCall);
    }

    public function newStringMessage(string $value = 'a string')
    {
        $message = new StringValue();
        $message->setValue($value);
        return $message;
    }

    public function testRead()
    {
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_RECV_MESSAGE => true,
            ]))->willReturn(new StartBatchEvent($message->serializeToString()));

        $serverCallReader = new ServerCallReader(
            $this->mockCall,
            StringValue::class
        );
        $return = $serverCallReader->read();
        $this->assertEquals($message, $return);
    }

    public function testStartEmptyMetadata()
    {
        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_SEND_INITIAL_METADATA => [],
            ]));

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $this->serverContext->setInitialMetadata([]);
        $serverCallWriter->start();
    }

    public function testStartWithMetadata()
    {
        $metadata = ['a' => 1];

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_SEND_INITIAL_METADATA => $metadata,
            ]));

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $this->serverContext->setInitialMetadata($metadata);
        $serverCallWriter->start();
        return $serverCallWriter;
    }

    public function testStartWithMessage()
    {
        $metadata = ['a' => 1];
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_SEND_INITIAL_METADATA => $metadata,
                OP_SEND_MESSAGE => ['message' => $message->serializeToString()],
            ]));

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $this->serverContext->setInitialMetadata($metadata);
        $serverCallWriter->start($message);
    }

    public function testWriteStartWithMessageAndOptions()
    {
        $metadata = ['a' => 1];
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_SEND_INITIAL_METADATA => $metadata,
                OP_SEND_MESSAGE => [
                    'message' => $message->serializeToString(),
                    'flags' => 0x02,
                ],
            ]));

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $this->serverContext->setInitialMetadata($metadata);
        $serverCallWriter->start($message, ['flags' => 0x02]);
    }

    public function testWriteDataOnly()
    {
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_SEND_INITIAL_METADATA => [],
                OP_SEND_MESSAGE => ['message' => $message->serializeToString()],
            ]));

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $serverCallWriter->write($message);
    }

    public function testWriteDataWithOptions()
    {
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_SEND_INITIAL_METADATA => [],
                OP_SEND_MESSAGE => [
                    'message' => $message->serializeToString(),
                    'flags' => 0x02
                ],
            ]));

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $serverCallWriter->write($message, ['flags' => 0x02]);
    }

    public function testWriteDataWithMetadata()
    {
        $metadata = ['a' => 1];
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_SEND_INITIAL_METADATA => $metadata,
                OP_SEND_MESSAGE => ['message' => $message->serializeToString()],
            ]));

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $this->serverContext->setInitialMetadata($metadata);
        $serverCallWriter->write($message, []);
    }

    public function testFinish()
    {
        $status = Status::status(
            STATUS_INVALID_ARGUMENT,
            "invalid argument",
            ['trailingMeta' => 100]
        );

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_SEND_STATUS_FROM_SERVER => $status,
                OP_RECV_CLOSE_ON_SERVER => true,
                OP_SEND_INITIAL_METADATA => [],
            ]));

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $this->serverContext->setStatus($status);
        $serverCallWriter->finish();
    }

    public function testFinishWithMetadataAndMessage()
    {
        $metadata = ['a' => 1];
        $message = $this->newStringMessage();
        $status = Status::ok(['trailingMeta' => 100]);

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                OP_SEND_STATUS_FROM_SERVER => $status,
                OP_RECV_CLOSE_ON_SERVER => true,
                OP_SEND_INITIAL_METADATA => $metadata,
                OP_SEND_MESSAGE => [
                    'message' => $message->serializeToString(),
                    'flags' => 0x02,
                ],
            ]));

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $this->serverContext->setInitialMetadata($metadata);
        $this->serverContext->setStatus($status);
        $serverCallWriter->finish($message, ['flags' => 0x02]);
    }

    public function testStartWriteFinish()
    {
        $metadata = ['a' => 1];
        $metadata2 = ['a' => 2];
        $message1 = $this->newStringMessage();
        $message2 = $this->newStringMessage('another string');

        $invokedCount = $this->exactly(4);

        $this->mockCall->expects($invokedCount)
            ->method('startBatch')
            ->willReturnCallback(function($parameters) use ($invokedCount, $metadata, $message1, $message2) {
              if ($invokedCount->getInvocationCount() === 1) {
                $this->assertSame([
                  OP_SEND_INITIAL_METADATA => $metadata,
                ],$parameters);
              }

              if ($invokedCount->getInvocationCount() === 2) {
                $this->assertSame([
                  OP_SEND_MESSAGE => ['message' => $message1->serializeToString()],
                ],$parameters);
              }

              if ($invokedCount->getInvocationCount() === 3) {
                $this->assertSame([
                  OP_SEND_MESSAGE => [
                    'message' => $message2->serializeToString(),
                    'flags' => 0x02,
                  ]
                ],$parameters);
              }

              if ($invokedCount->getInvocationCount() === 4) {
                $this->assertSame([
                  OP_SEND_STATUS_FROM_SERVER => Status::ok(),
                  OP_RECV_CLOSE_ON_SERVER => true,
                ],$parameters);
              }
            });

        $serverCallWriter = new ServerCallWriter(
            $this->mockCall,
            $this->serverContext
        );
        $this->serverContext->setInitialMetadata($metadata);
        $serverCallWriter->start();
        $serverCallWriter->write($message1, [], $metadata2 /* should not send */);
        $serverCallWriter->write($message2, ['flags' => 0x02]);
        $serverCallWriter->finish();
    }
}
