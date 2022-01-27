#region Copyright notice and license

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

#endregion

using System.Collections.Generic;
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
            await requestStream.ForEachAsync(num =>
            {
                sum += num.Num_;
                return TaskUtils.CompletedTask;
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
