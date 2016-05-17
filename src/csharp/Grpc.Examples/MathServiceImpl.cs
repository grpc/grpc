#region Copyright notice and license

// Copyright 2015-2016, Google Inc.
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
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Math
{
    /// <summary>
    /// Implementation of MathService server
    /// </summary>
    public class MathServiceImpl : Math.MathBase
    {
        public override Task<DivReply> Div(DivArgs request, ServerCallContext context)
        {
            return Task.FromResult(DivInternal(request));
        }

        public override async Task Fib(FibArgs request, IServerStreamWriter<Num> responseStream, ServerCallContext context)
        {
            var limit = request.Limit > 0 ? request.Limit : long.MaxValue;
            var fibEnumerator = FibInternal(limit).GetEnumerator();

            // Keep streaming the sequence until the call is cancelled.
            // Use CancellationToken from ServerCallContext to detect the cancellation.
            while (!context.CancellationToken.IsCancellationRequested && fibEnumerator.MoveNext())
            {
                await responseStream.WriteAsync(fibEnumerator.Current);
                await Task.Delay(100);
            }
        }

        public override async Task<Num> Sum(IAsyncStreamReader<Num> requestStream, ServerCallContext context)
        {
            long sum = 0;
            await requestStream.ForEachAsync(async num =>
            {
                sum += num.Num_;
            });
            return new Num { Num_ = sum };
        }

        public override async Task DivMany(IAsyncStreamReader<DivArgs> requestStream, IServerStreamWriter<DivReply> responseStream, ServerCallContext context)
        {
            await requestStream.ForEachAsync(async divArgs => await responseStream.WriteAsync(DivInternal(divArgs)));
        }

        static DivReply DivInternal(DivArgs args)
        {
            if (args.Divisor == 0)
            {
                // One can finish the RPC with non-ok status by throwing RpcException instance.
                // Alternatively, resulting status can be set using ServerCallContext.Status
                throw new RpcException(new Status(StatusCode.InvalidArgument, "Division by zero"));
            }

            long quotient = args.Dividend / args.Divisor;
            long remainder = args.Dividend % args.Divisor;
            return new DivReply { Quotient = quotient, Remainder = remainder };
        }

        static IEnumerable<Num> FibInternal(long n)
        {
            long a = 1;
            yield return new Num { Num_ = a };

            long b = 1;
            for (long i = 0; i < n - 1; i++)
            {
                long temp = a;
                a = b;
                b = temp + b;
                yield return new Num { Num_ = a };
            }
        }        
    }
}
