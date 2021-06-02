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
 * A service used to obtain stats for verifying LB behavior.
 */
class LoadBalancerStatsServiceStub {

    /**
     * Gets the backend distribution for RPCs sent by a test client.
     * @param \Grpc\Testing\LoadBalancerStatsRequest $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Grpc\Testing\LoadBalancerStatsResponse for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function GetClientStats(
        \Grpc\Testing\LoadBalancerStatsRequest $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\LoadBalancerStatsResponse {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * Gets the accumulated stats for RPCs sent by a test client.
     * @param \Grpc\Testing\LoadBalancerAccumulatedStatsRequest $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Grpc\Testing\LoadBalancerAccumulatedStatsResponse for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function GetClientAccumulatedStats(
        \Grpc\Testing\LoadBalancerAccumulatedStatsRequest $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\LoadBalancerAccumulatedStatsResponse {
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
            '/grpc.testing.LoadBalancerStatsService/GetClientStats' => new \Grpc\MethodDescriptor(
                $this,
                'GetClientStats',
                '\Grpc\Testing\LoadBalancerStatsRequest',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
            '/grpc.testing.LoadBalancerStatsService/GetClientAccumulatedStats' => new \Grpc\MethodDescriptor(
                $this,
                'GetClientAccumulatedStats',
                '\Grpc\Testing\LoadBalancerAccumulatedStatsRequest',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
        ];
    }

}
