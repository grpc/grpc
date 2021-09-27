<?php
// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
// Copyright 2015 gRPC authors.
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
namespace Math;

/**
 */
class MathStub {

    /**
     * Div divides DivArgs.dividend by DivArgs.divisor and returns the quotient
     * and remainder.
     * @param \Math\DivArgs $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Math\DivReply for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function Div(
        \Math\DivArgs $request,
        \Grpc\ServerContext $context
    ): ?\Math\DivReply {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * DivMany accepts an arbitrary number of division args from the client stream
     * and sends back the results in the reply stream.  The stream continues until
     * the client closes its end; the server does the same after sending all the
     * replies.  The stream ends immediately if either end aborts.
     * @param \Grpc\ServerCallReader $reader read client request data of \Math\DivArgs
     * @param \Grpc\ServerCallWriter $writer write response data of \Math\DivReply
     * @param \Grpc\ServerContext $context server request context
     * @return void
     */
    public function DivMany(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerCallWriter $writer,
        \Grpc\ServerContext $context
    ): void {
        $context->setStatus(\Grpc\Status::unimplemented());
        $writer->finish();
    }

    /**
     * Fib generates numbers in the Fibonacci sequence.  If FibArgs.limit > 0, Fib
     * generates up to limit numbers; otherwise it continues until the call is
     * canceled.  Unlike Fib above, Fib has no final FibReply.
     * @param \Math\FibArgs $request client request
     * @param \Grpc\ServerCallWriter $writer write response data of \Math\Num
     * @param \Grpc\ServerContext $context server request context
     * @return void
     */
    public function Fib(
        \Math\FibArgs $request,
        \Grpc\ServerCallWriter $writer,
        \Grpc\ServerContext $context
    ): void {
        $context->setStatus(\Grpc\Status::unimplemented());
        $writer->finish();
    }

    /**
     * Sum sums a stream of numbers, returning the final result once the stream
     * is closed.
     * @param \Grpc\ServerCallReader $reader read client request data of \Math\Num
     * @param \Grpc\ServerContext $context server request context
     * @return \Math\Num for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function Sum(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerContext $context
    ): ?\Math\Num {
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
            '/math.Math/Div' => new \Grpc\MethodDescriptor(
                $this,
                'Div',
                '\Math\DivArgs',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
            '/math.Math/DivMany' => new \Grpc\MethodDescriptor(
                $this,
                'DivMany',
                '\Math\DivArgs',
                \Grpc\MethodDescriptor::BIDI_STREAMING_CALL
            ),
            '/math.Math/Fib' => new \Grpc\MethodDescriptor(
                $this,
                'Fib',
                '\Math\FibArgs',
                \Grpc\MethodDescriptor::SERVER_STREAMING_CALL
            ),
            '/math.Math/Sum' => new \Grpc\MethodDescriptor(
                $this,
                'Sum',
                '\Math\Num',
                \Grpc\MethodDescriptor::CLIENT_STREAMING_CALL
            ),
        ];
    }

}
