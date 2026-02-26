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

use Grpc\BaseStub;
use Grpc\MethodDescriptor;
use Grpc\RpcServer;
use PHPUnit\Framework\MockObject\MockObject;
use PHPUnit\Framework\TestCase;

class RpcServerTest extends TestCase
{
    /**
     * @var RpcServer
     */
    private $server;

    /**
     * @var BaseStub&MockObject
     */
    private $mockService;

    public function setUp(): void
    {
        $this->server = new RpcServer();
        $this->mockService = $this->getMockBuilder(BaseStub::class)
            ->disableOriginalConstructor()
            ->addMethods(['getMethodDescriptors', 'hello'])
            ->getMock();
    }

    public function testHandleServices()
    {
        $helloMethodDescriptor = new MethodDescriptor(
            $this->mockService,
            'hello',
            'String',
            MethodDescriptor::UNARY_CALL
        );
        $this->mockService->expects($this->once())
            ->method('getMethodDescriptors')
            ->with()
            ->will($this->returnValue([
                '/test/hello' => $helloMethodDescriptor
            ]));

        $pathMap = $this->server->handle($this->mockService);
        $this->assertEquals($pathMap, [
            '/test/hello' => $helloMethodDescriptor
        ]);

        $mockService2 = $this->getMockBuilder(BaseStub::class)
            ->disableOriginalConstructor()
            ->addMethods(['getMethodDescriptors', 'hello', 'bye'])
            ->getMock();
        $helloMethodDescriptor2 = new MethodDescriptor(
            $this->mockService,
            'hello',
            'Number',
            MethodDescriptor::UNARY_CALL
        );
        $byeMethodDescritor = new MethodDescriptor(
            $this->mockService,
            'bye',
            'String',
            MethodDescriptor::UNARY_CALL
        );
        $mockService2->expects($this->once())
            ->method('getMethodDescriptors')
            ->with()
            ->will($this->returnValue([
                '/test/hello' => $helloMethodDescriptor2,
                '/test/bye' => $byeMethodDescritor
            ]));

        $pathMap = $this->server->handle($mockService2);
        $this->assertEquals($pathMap, [
            '/test/hello' => $helloMethodDescriptor2,
            '/test/bye' => $byeMethodDescritor
        ]);
    }
}
