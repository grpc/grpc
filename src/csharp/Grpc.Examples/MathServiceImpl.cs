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
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;

namespace math
{
    /// <summary>
    /// Implementation of MathService server
    /// </summary>
    public class MathServiceImpl : Math.IMath
    {
        public Task<DivReply> Div(ServerCallContext context, DivArgs request)
        {
            return Task.FromResult(DivInternal(request));
        }

        public async Task Fib(ServerCallContext context, FibArgs request, IServerStreamWriter<Num> responseStream)
        {
            if (request.Limit <= 0)
            {
                // TODO(jtattermusch): support cancellation
                throw new NotImplementedException("Not implemented yet");
            }

            if (request.Limit > 0)
            {
                foreach (var num in FibInternal(request.Limit))
                {
                    await responseStream.WriteAsync(num);
                }
            }
        }

        public async Task<Num> Sum(ServerCallContext context, IAsyncStreamReader<Num> requestStream)
        {
            long sum = 0;
            await requestStream.ForEach(async num =>
            {
                sum += num.Num_;
            });
            return Num.CreateBuilder().SetNum_(sum).Build();
        }

        public async Task DivMany(ServerCallContext context, IAsyncStreamReader<DivArgs> requestStream, IServerStreamWriter<DivReply> responseStream)
        {
            await requestStream.ForEach(async divArgs =>
            {
                await responseStream.WriteAsync(DivInternal(divArgs));
            });
        }

        static DivReply DivInternal(DivArgs args)
        {
            long quotient = args.Dividend / args.Divisor;
            long remainder = args.Dividend % args.Divisor;
            return new DivReply.Builder { Quotient = quotient, Remainder = remainder }.Build();
        }

        static IEnumerable<Num> FibInternal(long n)
        {
            long a = 1;
            yield return new Num.Builder { Num_ = a }.Build();

            long b = 1;
            for (long i = 0; i < n - 1; i++)
            {
                long temp = a;
                a = b;
                b = temp + b;
                yield return new Num.Builder { Num_ = a }.Build();
            }
        }        
    }
}
