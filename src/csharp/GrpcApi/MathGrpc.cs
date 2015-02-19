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
using Grpc.Core;

namespace math
{
    /// <summary>
    /// Math service definitions (this is handwritten version of code that will normally be generated).
    /// </summary>
    public class MathGrpc
    {
        readonly static Marshaller<DivArgs> divArgsMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), DivArgs.ParseFrom);
        readonly static Marshaller<DivReply> divReplyMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), DivReply.ParseFrom);
        readonly static Marshaller<Num> numMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), Num.ParseFrom);
        readonly static Marshaller<FibArgs> fibArgsMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), FibArgs.ParseFrom);

        readonly static Method<DivArgs, DivReply> divMethod = new Method<DivArgs, DivReply>(
            MethodType.Unary,
            "/math.Math/Div",
            divArgsMarshaller,
            divReplyMarshaller
        );
        readonly static Method<FibArgs, Num> fibMethod = new Method<FibArgs, Num>(
            MethodType.ServerStreaming,
            "/math.Math/Fib",
            fibArgsMarshaller,
            numMarshaller
        );
        readonly static Method<Num, Num> sumMethod = new Method<Num, Num>(
            MethodType.ClientStreaming,
            "/math.Math/Sum",
            numMarshaller,
            numMarshaller
        );
        readonly static Method<DivArgs, DivReply> divManyMethod = new Method<DivArgs, DivReply>(
            MethodType.DuplexStreaming,
            "/math.Math/DivMany",
            divArgsMarshaller,
            divReplyMarshaller
        );

        public interface IMathServiceClient
        {
            DivReply Div(DivArgs request, CancellationToken token = default(CancellationToken));

            Task<DivReply> DivAsync(DivArgs request, CancellationToken token = default(CancellationToken));

            void Fib(FibArgs request, IObserver<Num> responseObserver, CancellationToken token = default(CancellationToken));

            ClientStreamingAsyncResult<Num, Num> Sum(CancellationToken token = default(CancellationToken));

            IObserver<DivArgs> DivMany(IObserver<DivReply> responseObserver, CancellationToken token = default(CancellationToken));
        }

        public class MathServiceClientStub : IMathServiceClient
        {
            readonly Channel channel;

            public MathServiceClientStub(Channel channel)
            {
                this.channel = channel;
            }

            public DivReply Div(DivArgs request, CancellationToken token = default(CancellationToken))
            {
                var call = new Grpc.Core.Call<DivArgs, DivReply>(divMethod, channel);
                return Calls.BlockingUnaryCall(call, request, token);
            }

            public Task<DivReply> DivAsync(DivArgs request, CancellationToken token = default(CancellationToken))
            {
                var call = new Grpc.Core.Call<DivArgs, DivReply>(divMethod, channel);
                return Calls.AsyncUnaryCall(call, request, token);
            }

            public void Fib(FibArgs request, IObserver<Num> responseObserver, CancellationToken token = default(CancellationToken))
            {
                var call = new Grpc.Core.Call<FibArgs, Num>(fibMethod, channel);
                Calls.AsyncServerStreamingCall(call, request, responseObserver, token);
            }

            public ClientStreamingAsyncResult<Num, Num> Sum(CancellationToken token = default(CancellationToken))
            {
                var call = new Grpc.Core.Call<Num, Num>(sumMethod, channel);
                return Calls.AsyncClientStreamingCall(call, token);
            }

            public IObserver<DivArgs> DivMany(IObserver<DivReply> responseObserver, CancellationToken token = default(CancellationToken))
            {
                var call = new Grpc.Core.Call<DivArgs, DivReply>(divManyMethod, channel);
                return Calls.DuplexStreamingCall(call, responseObserver, token);
            }
        }

        // server-side interface
        public interface IMathService
        {
            void Div(DivArgs request, IObserver<DivReply> responseObserver);

            void Fib(FibArgs request, IObserver<Num> responseObserver);

            IObserver<Num> Sum(IObserver<Num> responseObserver);

            IObserver<DivArgs> DivMany(IObserver<DivReply> responseObserver);
        }

        public static ServerServiceDefinition BindService(IMathService serviceImpl)
        {
            return ServerServiceDefinition.CreateBuilder("/math.Math/")
                .AddMethod(divMethod, serviceImpl.Div)
                .AddMethod(fibMethod, serviceImpl.Fib)
                .AddMethod(sumMethod, serviceImpl.Sum)
                .AddMethod(divManyMethod, serviceImpl.DivMany).Build();
        }

        public static IMathServiceClient NewStub(Channel channel)
        {
            return new MathServiceClientStub(channel);
        }
    }
}
