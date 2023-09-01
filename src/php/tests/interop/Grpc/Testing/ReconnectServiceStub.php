<?php
// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
// Copyright 2015-2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// An integration test service that covers all the method signature permutations
// of unary/streaming requests/responses.
//
namespace Grpc\Testing;

/**
 * A service used to control reconnect server.
 */
class ReconnectServiceStub {

    /**
     * @param \Grpc\Testing\ReconnectParams $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Grpc\Testing\EmptyMessage for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function Start(
        \Grpc\Testing\ReconnectParams $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\EmptyMessage {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * @param \Grpc\Testing\EmptyMessage $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Grpc\Testing\ReconnectInfo for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function Stop(
        \Grpc\Testing\EmptyMessage $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\ReconnectInfo {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * Get the method descriptors of the service for server registration
     *
     * @return array of \Grpc\MethodDescriptor for the service methods
     */
    public final function getMethodDescriptors(): array
    {
        return [
            '/grpc.testing.ReconnectService/Start' => new \Grpc\MethodDescriptor(
                $this,
                'Start',
                '\Grpc\Testing\ReconnectParams',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
            '/grpc.testing.ReconnectService/Stop' => new \Grpc\MethodDescriptor(
                $this,
                'Stop',
                '\Grpc\Testing\EmptyMessage',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
        ];
    }

}
