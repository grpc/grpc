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
        static readonly string ServiceName = "/math.Math";

        static readonly Marshaller<DivArgs> DivArgsMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), DivArgs.ParseFrom);
        static readonly Marshaller<DivReply> DivReplyMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), DivReply.ParseFrom);
        static readonly Marshaller<Num> NumMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), Num.ParseFrom);
        static readonly Marshaller<FibArgs> FibArgsMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), FibArgs.ParseFrom);

        static readonly Method<DivArgs, DivReply> DivMethod = new Method<DivArgs, DivReply>(
            MethodType.Unary,
            "Div",
            DivArgsMarshaller,
            DivReplyMarshaller);

        static readonly Method<FibArgs, Num> FibMethod = new Method<FibArgs, Num>(
            MethodType.ServerStreaming,
            "Fib",
            FibArgsMarshaller,
            NumMarshaller);

        static readonly Method<Num, Num> SumMethod = new Method<Num, Num>(
            MethodType.ClientStreaming,
            "Sum",
            NumMarshaller,
            NumMarshaller);

        static readonly Method<DivArgs, DivReply> DivManyMethod = new Method<DivArgs, DivReply>(
            MethodType.DuplexStreaming,
            "DivMany",
            DivArgsMarshaller,
            DivReplyMarshaller);

        public interface IMathServiceClient
        {
            DivReply Div(DivArgs request, CancellationToken token = default(CancellationToken));

            Task<DivReply> DivAsync(DivArgs request, CancellationToken token = default(CancellationToken));

            AsyncServerStreamingCall<Num> Fib(FibArgs request, CancellationToken token = default(CancellationToken));

            AsyncClientStreamingCall<Num, Num> Sum(CancellationToken token = default(CancellationToken));

            AsyncDuplexStreamingCall<DivArgs, DivReply> DivMany(CancellationToken token = default(CancellationToken));
        }

        public class MathServiceClientStub : AbstractStub<MathServiceClientStub, StubConfiguration>, IMathServiceClient
        {
            public MathServiceClientStub(Channel channel) : this(channel, StubConfiguration.Default)
            {
            }

            public MathServiceClientStub(Channel channel, StubConfiguration config) : base(channel, config)
            {
            }

            public DivReply Div(DivArgs request, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, DivMethod);
                return Calls.BlockingUnaryCall(call, request, token);
            }

            public Task<DivReply> DivAsync(DivArgs request, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, DivMethod);
                return Calls.AsyncUnaryCall(call, request, token);
            }

            public AsyncServerStreamingCall<Num> Fib(FibArgs request, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, FibMethod);
                return Calls.AsyncServerStreamingCall(call, request, token);
            }

            public AsyncClientStreamingCall<Num, Num> Sum(CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, SumMethod);
                return Calls.AsyncClientStreamingCall(call, token);
            }

            public AsyncDuplexStreamingCall<DivArgs, DivReply> DivMany(CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, DivManyMethod);
                return Calls.AsyncDuplexStreamingCall(call, token);
            }
        }

        // server-side interface
        public interface IMathService
        {
            Task<DivReply> Div(ServerCallContext context, DivArgs request);

            Task Fib(ServerCallContext context, FibArgs request, IServerStreamWriter<Num> responseStream);

            Task<Num> Sum(ServerCallContext context, IAsyncStreamReader<Num> requestStream);

            Task DivMany(ServerCallContext context, IAsyncStreamReader<DivArgs> requestStream, IServerStreamWriter<DivReply> responseStream);
        }

        public static ServerServiceDefinition BindService(IMathService serviceImpl)
        {
            return ServerServiceDefinition.CreateBuilder(ServiceName)
                .AddMethod(DivMethod, serviceImpl.Div)
                .AddMethod(FibMethod, serviceImpl.Fib)
                .AddMethod(SumMethod, serviceImpl.Sum)
                .AddMethod(DivManyMethod, serviceImpl.DivMany).Build();
        }

        public static IMathServiceClient NewStub(Channel channel)
        {
            return new MathServiceClientStub(channel);
        }

        public static IMathServiceClient NewStub(Channel channel, StubConfiguration config)
        {
            return new MathServiceClientStub(channel, config);
        }
    }
}
