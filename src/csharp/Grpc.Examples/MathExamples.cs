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
using System.Threading.Tasks;
using Grpc.Core.Utils;

namespace math
{
    public static class MathExamples
    {
        public static void DivExample(Math.IMathClient stub)
        {
            DivReply result = stub.Div(new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build());
            Console.WriteLine("Div Result: " + result);
        }

        public static async Task DivAsyncExample(Math.IMathClient stub)
        {
            Task<DivReply> resultTask = stub.DivAsync(new DivArgs.Builder { Dividend = 4, Divisor = 5 }.Build());
            DivReply result = await resultTask;
            Console.WriteLine("DivAsync Result: " + result);
        }

        public static async Task FibExample(Math.IMathClient stub)
        {
            using (var call = stub.Fib(new FibArgs.Builder { Limit = 5 }.Build()))
            {
                List<Num> result = await call.ResponseStream.ToList();
                Console.WriteLine("Fib Result: " + string.Join("|", result));
            }
        }

        public static async Task SumExample(Math.IMathClient stub)
        {
            var numbers = new List<Num>
            {
                new Num.Builder { Num_ = 1 }.Build(),
                new Num.Builder { Num_ = 2 }.Build(),
                new Num.Builder { Num_ = 3 }.Build()
            };

            using (var call = stub.Sum())
            {
                await call.RequestStream.WriteAll(numbers);
                Console.WriteLine("Sum Result: " + await call.Result);
            }
        }

        public static async Task DivManyExample(Math.IMathClient stub)
        {
            var divArgsList = new List<DivArgs>
            {
                new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build(),
                new DivArgs.Builder { Dividend = 100, Divisor = 21 }.Build(),
                new DivArgs.Builder { Dividend = 7, Divisor = 2 }.Build()
            };
            using (var call = stub.DivMany())
            { 
                await call.RequestStream.WriteAll(divArgsList);
                Console.WriteLine("DivMany Result: " + string.Join("|", await call.ResponseStream.ToList()));
            }
        }

        public static async Task DependendRequestsExample(Math.IMathClient stub)
        {
            var numbers = new List<Num>
            {
                new Num.Builder { Num_ = 1 }.Build(), 
                new Num.Builder { Num_ = 2 }.Build(),
                new Num.Builder { Num_ = 3 }.Build()
            };

            Num sum;
            using (var sumCall = stub.Sum())
            {
                await sumCall.RequestStream.WriteAll(numbers);
                sum = await sumCall.Result;
            }

            DivReply result = await stub.DivAsync(new DivArgs.Builder { Dividend = sum.Num_, Divisor = numbers.Count }.Build());
            Console.WriteLine("Avg Result: " + result);
        }
    }
}
