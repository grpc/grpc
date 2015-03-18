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
using System.Reactive.Linq;
using System.Threading.Tasks;
using Grpc.Core.Utils;

namespace math
{
    public static class MathExamples
    {
        public static void DivExample(MathGrpc.IMathServiceClient stub)
        {
            DivReply result = stub.Div(new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build());
            Console.WriteLine("Div Result: " + result);
        }

        public static void DivAsyncExample(MathGrpc.IMathServiceClient stub)
        {
            Task<DivReply> call = stub.DivAsync(new DivArgs.Builder { Dividend = 4, Divisor = 5 }.Build());
            DivReply result = call.Result;
            Console.WriteLine(result);
        }

        public static void DivAsyncWithCancellationExample(MathGrpc.IMathServiceClient stub)
        {
            Task<DivReply> call = stub.DivAsync(new DivArgs.Builder { Dividend = 4, Divisor = 5 }.Build());
            DivReply result = call.Result;
            Console.WriteLine(result);
        }

        public static void FibExample(MathGrpc.IMathServiceClient stub)
        {
            var recorder = new RecordingObserver<Num>();
            stub.Fib(new FibArgs.Builder { Limit = 5 }.Build(), recorder);

            List<Num> numbers = recorder.ToList().Result;
            Console.WriteLine("Fib Result: " + string.Join("|", recorder.ToList().Result));
        }

        public static void SumExample(MathGrpc.IMathServiceClient stub)
        {
            List<Num> numbers = new List<Num>
            {
                new Num.Builder { Num_ = 1 }.Build(),
                new Num.Builder { Num_ = 2 }.Build(),
                new Num.Builder { Num_ = 3 }.Build()
            };

            var res = stub.Sum();
            foreach (var num in numbers)
            {
                res.Inputs.OnNext(num);
            }
            res.Inputs.OnCompleted();

            Console.WriteLine("Sum Result: " + res.Task.Result);
        }

        public static void DivManyExample(MathGrpc.IMathServiceClient stub)
        {
            List<DivArgs> divArgsList = new List<DivArgs>
            {
                new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build(),
                new DivArgs.Builder { Dividend = 100, Divisor = 21 }.Build(),
                new DivArgs.Builder { Dividend = 7, Divisor = 2 }.Build()
            };

            var recorder = new RecordingObserver<DivReply>();

            var inputs = stub.DivMany(recorder);
            foreach (var input in divArgsList)
            {
                inputs.OnNext(input);
            }
            inputs.OnCompleted();

            Console.WriteLine("DivMany Result: " + string.Join("|", recorder.ToList().Result));
        }

        public static void DependendRequestsExample(MathGrpc.IMathServiceClient stub)
        {
            var numberList = new List<Num>
            {
                new Num.Builder { Num_ = 1 }.Build(),
                new Num.Builder { Num_ = 2 }.Build(), new Num.Builder { Num_ = 3 }.Build()
            };

            numberList.ToObservable();
        }
    }
}
