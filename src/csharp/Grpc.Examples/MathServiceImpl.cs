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
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Utils;

namespace math
{
    /// <summary>
    /// Implementation of MathService server
    /// </summary>
    public class MathServiceImpl : MathGrpc.IMathService
    {
        public void Div(DivArgs request, IObserver<DivReply> responseObserver)
        {
            var response = DivInternal(request);
            responseObserver.OnNext(response);
            responseObserver.OnCompleted();
        }

        public void Fib(FibArgs request, IObserver<Num> responseObserver)
        {
            if (request.Limit <= 0)
            {
                // TODO: support cancellation....
                throw new NotImplementedException("Not implemented yet");
            }

            if (request.Limit > 0)
            {
                foreach (var num in FibInternal(request.Limit))
                {
                    responseObserver.OnNext(num);
                }
                responseObserver.OnCompleted();
            }
        }

        public IObserver<Num> Sum(IObserver<Num> responseObserver)
        {
            var recorder = new RecordingObserver<Num>();
            Task.Factory.StartNew(() => {

                List<Num> inputs = recorder.ToList().Result;

                long sum = 0;
                foreach (Num num in inputs)
                {
                    sum += num.Num_;
                }

                responseObserver.OnNext(Num.CreateBuilder().SetNum_(sum).Build());
                responseObserver.OnCompleted();
            });
            return recorder;
        }

        public IObserver<DivArgs> DivMany(IObserver<DivReply> responseObserver)
        {
            return new DivObserver(responseObserver);
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
            yield return new Num.Builder { Num_=a }.Build();

            long b = 1;
            for (long i = 0; i < n - 1; i++)
            {
                long temp = a;
                a = b;
                b = temp + b;
                yield return new Num.Builder { Num_=a }.Build();
            }
        }

        private class DivObserver : IObserver<DivArgs> {

            readonly IObserver<DivReply> responseObserver;

            public DivObserver(IObserver<DivReply> responseObserver)
            {
                this.responseObserver = responseObserver;
            }

            public void OnCompleted()
            {
                Task.Factory.StartNew(() =>
                    responseObserver.OnCompleted());
            }

            public void OnError(Exception error)
            {
                throw new NotImplementedException();
            }

            public void OnNext(DivArgs value)
            {
                // TODO: currently we need this indirection because
                // responseObserver waits for write to finish, this
                // callback is called from grpc threadpool which
                // currently only has one thread.
                // Same story for OnCompleted().
                Task.Factory.StartNew(() =>
                responseObserver.OnNext(DivInternal(value)));
            }
        }
    }
}

