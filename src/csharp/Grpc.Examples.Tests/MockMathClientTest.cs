#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endregion

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Core.Testing;
using NUnit.Framework;

namespace Math.Tests
{
    /// <summary>
    /// Example of mocking a gRPC service client for unit testing purposes.
    /// </summary>
    public class MockMathClientTest
    {
        FakeMathClient mockClient = new FakeMathClient();

        [Test]
        public async void MockAsyncUnaryCall()
        {
            var expectedResponse = new DivReply
            {
                Quotient = 3,
                Remainder = 1
            };
            var mockCall = new MockAsyncUnaryCall<DivReply>();
            mockCall.SetResponse(expectedResponse);

            mockClient.DivAsyncImpl = (request, options) =>
            {
                return mockCall.Call;
            };

            var response = await mockClient.DivAsync(new DivArgs { Dividend = 10, Divisor = 3 }, new CallOptions());
            Assert.AreEqual(expectedResponse, response);
        }

        /// <summary>
        /// Demonstrates how to create a fake implementation of IMathClient.
        /// </summary>
        class FakeMathClient : Math.IMathClient
        {
            public Func<DivArgs, CallOptions, DivReply> DivImpl;
            public Func<DivArgs, CallOptions, AsyncUnaryCall<DivReply>> DivAsyncImpl;

            public DivReply Div(DivArgs request, Metadata headers, DateTime? deadline, CancellationToken cancellationToken)
            {
                return Div(request, new CallOptions(headers: headers, deadline: deadline, cancellationToken: cancellationToken));
            }

            public DivReply Div(DivArgs request, CallOptions options)
            {
                return DivImpl(request, options);
            }

            public AsyncUnaryCall<DivReply> DivAsync(DivArgs request, Metadata headers, DateTime? deadline, CancellationToken cancellationToken)
            {
                return DivAsync(request, new CallOptions(headers: headers, deadline: deadline, cancellationToken: cancellationToken));
            }

            public AsyncUnaryCall<DivReply> DivAsync(DivArgs request, CallOptions options)
            {
                return DivAsyncImpl(request, options);
            }

            public AsyncDuplexStreamingCall<DivArgs, DivReply> DivMany(Metadata headers, DateTime? deadline, CancellationToken cancellationToken)
            {
                return DivMany(new CallOptions(headers: headers, deadline: deadline, cancellationToken: cancellationToken));
            }

            public AsyncDuplexStreamingCall<DivArgs, DivReply> DivMany(CallOptions options)
            {
                throw new NotImplementedException();
            }

            public AsyncServerStreamingCall<Num> Fib(FibArgs request, Metadata headers, DateTime? deadline, CancellationToken cancellationToken)
            {
                return Fib(request, new CallOptions(headers: headers, deadline: deadline, cancellationToken: cancellationToken));
            }

            public AsyncServerStreamingCall<Num> Fib(FibArgs request, CallOptions options)
            {
                throw new NotImplementedException();
            }

            public AsyncClientStreamingCall<Num, Num> Sum(Metadata headers, DateTime? deadline, CancellationToken cancellationToken)
            {
                return Sum(new CallOptions(headers: headers, deadline: deadline, cancellationToken: cancellationToken));
            }

            public AsyncClientStreamingCall<Num, Num> Sum(CallOptions options)
            {
                throw new NotImplementedException();
            }
        }
    }
}
