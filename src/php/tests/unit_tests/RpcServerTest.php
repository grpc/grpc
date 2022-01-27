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
require_once(dirname(__FILE__) . '/../../lib/Grpc/MethodDescriptor.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/RpcServer.php');

class RpcServerTest extends \PHPUnit\Framework\TestCase
{
    public function setUp(): void
    {
        $this->server = new \Grpc\RpcServer();
        $this->mockService = $this->getMockBuilder(stdClass::class)
            ->setMethods(['getMethodDescriptors', 'hello'])
            ->getMock();
    }

    public function testHandleServices()
    {
        $helloMethodDescriptor = new \Grpc\MethodDescriptor(
            $this->mockService,
            'hello',
            'String',
            \Grpc\MethodDescriptor::UNARY_CALL
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

        $mockService2 = $this->getMockBuilder(stdClass::class)
            ->setMethods(['getMethodDescriptors', 'hello', 'bye'])
            ->getMock();
        $helloMethodDescriptor2 = new \Grpc\MethodDescriptor(
            $this->mockService,
            'hello',
            'Number',
            \Grpc\MethodDescriptor::UNARY_CALL
        );
        $byeMethodDescritor = new \Grpc\MethodDescriptor(
            $this->mockService,
            'bye',
            'String',
            \Grpc\MethodDescriptor::UNARY_CALL
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
