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

require_once(dirname(__FILE__) . '/../../lib/Grpc/ServerCallReader.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/ServerCallWriter.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/Status.php');

// load protobuf from third_party
set_include_path(get_include_path() . PATH_SEPARATOR . dirname(__FILE__) . '/../../../../third_party/protobuf/php/src/');

spl_autoload_register(function ($className) {
$classPath = str_replace('\\', DIRECTORY_SEPARATOR, $className);
if (strpos($classPath, 'Google/Protobuf') === 0 || strpos($classPath, 'GPBMetadata/Google/Protobuf') === 0) {
require_once($classPath . '.php');
}
});

class StartBatchEvent
{
    public function __construct(string $message)
    {
        $this->message = $message;
    }
    public $message;
}

class ServerCallTest extends \PHPUnit\Framework\TestCase
{
    public function setUp(): void
    {
        $this->mockCall = $this->getMockBuilder(stdClass::class)
            ->setMethods(['startBatch'])
            ->getMock();
    }

    public function newStringMessage(string $value = 'a string')
    {
        $message = new \Google\Protobuf\StringValue();
        $message->setValue($value);
        return $message;
    }

    public function testRead()
    {
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_RECV_MESSAGE => true,
            ]))->willReturn(new StartBatchEvent($message->serializeToString()));

        $serverCallReader = new \Grpc\ServerCallReader(
            $this->mockCall,
            '\Google\Protobuf\StringValue'
        );
        $return = $serverCallReader->read();
        $this->assertEquals($message, $return);
    }

    public function testStartEmptyMetadata()
    {
        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_INITIAL_METADATA => [],
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->start([]);
    }

    public function testStartWithMetadata()
    {
        $metadata = ['a' => 1];

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_INITIAL_METADATA => $metadata,
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->start($metadata);
        return $serverCallWriter;
    }

    public function testStartWithMessage()
    {
        $metadata = ['a' => 1];
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_INITIAL_METADATA => $metadata,
                \Grpc\OP_SEND_MESSAGE => ['message' => $message->serializeToString()],
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->start($metadata, $message);
    }

    public function testWriteStartWithMessageAndOptions()
    {
        $metadata = ['a' => 1];
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_INITIAL_METADATA => $metadata,
                \Grpc\OP_SEND_MESSAGE => [
                    'message' => $message->serializeToString(),
                    'flags' => 0x02,
                ],
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->start($metadata, $message, ['flags' => 0x02]);
    }

    public function testWriteDataOnly()
    {
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_INITIAL_METADATA => [],
                \Grpc\OP_SEND_MESSAGE => ['message' => $message->serializeToString()],
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->write($message);
    }

    public function testWriteDataWithOptions()
    {
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_INITIAL_METADATA => [],
                \Grpc\OP_SEND_MESSAGE => [
                    'message' => $message->serializeToString(),
                    'flags' => 0x02
                ],
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->write($message, ['flags' => 0x02]);
    }

    public function testWriteDataWithMetadata()
    {
        $metadata = ['a' => 1];
        $message = $this->newStringMessage();

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_INITIAL_METADATA => $metadata,
                \Grpc\OP_SEND_MESSAGE => ['message' => $message->serializeToString()],
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->write($message, [], $metadata);
    }

    public function testFinish()
    {
        $status = \Grpc\Status::status(
            \Grpc\STATUS_INVALID_ARGUMENT,
            "invalid argument",
            ['trailiingMeta' => 100]
        );

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_STATUS_FROM_SERVER => $status,
                \Grpc\OP_RECV_CLOSE_ON_SERVER => true,
                \Grpc\OP_SEND_INITIAL_METADATA => [],
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->finish($status);
    }

    public function testFinishWithMetadataAndMessage()
    {
        $metadata = ['a' => 1];
        $message = $this->newStringMessage();
        $status = \Grpc\Status::ok(['trailiingMeta' => 100]);

        $this->mockCall->expects($this->once())
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_STATUS_FROM_SERVER => $status,
                \Grpc\OP_RECV_CLOSE_ON_SERVER => true,
                \Grpc\OP_SEND_INITIAL_METADATA => $metadata,
                \Grpc\OP_SEND_MESSAGE => [
                    'message' => $message->serializeToString(),
                    'flags' => 0x02,
                ],
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->finish($status, $metadata, $message, ['flags' => 0x02]);
    }

    public function testStartWriteFinish()
    {
        $metadata = ['a' => 1];
        $metadata2 = ['a' => 2];
        $message1 = $this->newStringMessage();
        $message2 = $this->newStringMessage('another string');

        $this->mockCall->expects($this->at(0))
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_INITIAL_METADATA => $metadata,
            ]));
        $this->mockCall->expects($this->at(1))
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_MESSAGE => ['message' => $message1->serializeToString()],
            ]));
        $this->mockCall->expects($this->at(2))
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_MESSAGE => [
                    'message' => $message2->serializeToString(),
                    'flags' => 0x02,
                ]
            ]));
        $this->mockCall->expects($this->at(3))
            ->method('startBatch')
            ->with($this->identicalTo([
                \Grpc\OP_SEND_STATUS_FROM_SERVER => \Grpc\Status::ok(),
                \Grpc\OP_RECV_CLOSE_ON_SERVER => true,
            ]));

        $serverCallWriter = new \Grpc\ServerCallWriter($this->mockCall);
        $serverCallWriter->start($metadata);
        $serverCallWriter->write($message1, [], $metadata2 /* should not send */);
        $serverCallWriter->write($message2, ['flags' => 0x02]);
        $serverCallWriter->finish();
    }
}
