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

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Math
{
    public static class MathExamples
    {
        public static void DivExample(Math.MathClient client)
        {
            DivReply result = client.Div(new DivArgs { Dividend = 10, Divisor = 3 });
            Console.WriteLine("Div Result: " + result);
        }

        public static async Task DivAsyncExample(Math.MathClient client)
        {
            DivReply result = await client.DivAsync(new DivArgs { Dividend = 4, Divisor = 5 });
            Console.WriteLine("DivAsync Result: " + result);
        }

        public static async Task FibExample(Math.MathClient client)
        {
            using (var call = client.Fib(new FibArgs { Limit = 5 }))
            {
                List<Num> result = await call.ResponseStream.ToListAsync();
                Console.WriteLine("Fib Result: " + string.Join("|", result));
            }
        }

        public static async Task SumExample(Math.MathClient client)
        {
            var numbers = new List<Num>
            {
                new Num { Num_ = 1 },
                new Num { Num_ = 2 },
                new Num { Num_ = 3 }
            };

            using (var call = client.Sum())
            {
                await call.RequestStream.WriteAllAsync(numbers);
                Console.WriteLine("Sum Result: " + await call.ResponseAsync);
            }
        }

        public static async Task DivManyExample(Math.MathClient client)
        {
            var divArgsList = new List<DivArgs>
            {
                new DivArgs { Dividend = 10, Divisor = 3 },
                new DivArgs { Dividend = 100, Divisor = 21 },
                new DivArgs { Dividend = 7, Divisor = 2 }
            };

            using (var call = client.DivMany())
            { 
                await call.RequestStream.WriteAllAsync(divArgsList);
                Console.WriteLine("DivMany Result: " + string.Join("|", await call.ResponseStream.ToListAsync()));
            }
        }

        public static async Task DependentRequestsExample(Math.MathClient client)
        {
            var numbers = new List<Num>
            {
                new Num { Num_ = 1 }, 
                new Num { Num_ = 2 },
                new Num { Num_ = 3 }
            };

            Num sum;
            using (var sumCall = client.Sum())
            {
                await sumCall.RequestStream.WriteAllAsync(numbers);
                sum = await sumCall.ResponseAsync;
            }

            DivReply result = await client.DivAsync(new DivArgs { Dividend = sum.Num_, Divisor = numbers.Count });
            Console.WriteLine("Avg Result: " + result);
        }

        /// <summary>
        /// Shows how to handle a call ending with non-OK status.
        /// </summary>
        public static async Task HandleErrorExample(Math.MathClient client)
        {
            try
            {
                 DivReply result = await client.DivAsync(new DivArgs { Dividend = 5, Divisor = 0 });
            }
            catch (RpcException ex)
            {
                Console.WriteLine(string.Format("RPC ended with status {0}", ex.Status));
            }
        }

        /// <summary>
        /// Shows how to send request headers and how to access response headers
        /// and response trailers.
        /// </summary>
        public static async Task MetadataExample(Math.MathClient client)
        {
            var requestHeaders = new Metadata
            {
                { "custom-header", "custom-value" }
            };

            var call = client.DivAsync(new DivArgs { Dividend = 5, Divisor = 0 }, requestHeaders);

            // Get response headers
            Metadata responseHeaders = await call.ResponseHeadersAsync;

            var result = await call;

            // Get response trailers after the call has finished.
            Metadata responseTrailers = call.GetTrailers();
        }
    }
}
