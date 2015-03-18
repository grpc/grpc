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

namespace grpc.testing
{
    /// <summary>
    /// TestService (this is handwritten version of code that will normally be generated).
    /// </summary>
    public class TestServiceGrpc
    {
        static readonly string ServiceName = "/grpc.testing.TestService";

        static readonly Marshaller<Empty> EmptyMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), Empty.ParseFrom);
        static readonly Marshaller<SimpleRequest> SimpleRequestMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), SimpleRequest.ParseFrom);
        static readonly Marshaller<SimpleResponse> SimpleResponseMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), SimpleResponse.ParseFrom);
        static readonly Marshaller<StreamingOutputCallRequest> StreamingOutputCallRequestMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), StreamingOutputCallRequest.ParseFrom);
        static readonly Marshaller<StreamingOutputCallResponse> StreamingOutputCallResponseMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), StreamingOutputCallResponse.ParseFrom);
        static readonly Marshaller<StreamingInputCallRequest> StreamingInputCallRequestMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), StreamingInputCallRequest.ParseFrom);
        static readonly Marshaller<StreamingInputCallResponse> StreamingInputCallResponseMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), StreamingInputCallResponse.ParseFrom);

        static readonly Method<Empty, Empty> EmptyCallMethod = new Method<Empty, Empty>(
            MethodType.Unary,
            "EmptyCall",
            EmptyMarshaller,
            EmptyMarshaller);

        static readonly Method<SimpleRequest, SimpleResponse> UnaryCallMethod = new Method<SimpleRequest, SimpleResponse>(
            MethodType.Unary,
            "UnaryCall",
            SimpleRequestMarshaller,
            SimpleResponseMarshaller);

        static readonly Method<StreamingOutputCallRequest, StreamingOutputCallResponse> StreamingOutputCallMethod = new Method<StreamingOutputCallRequest, StreamingOutputCallResponse>(
            MethodType.ServerStreaming,
            "StreamingOutputCall",
            StreamingOutputCallRequestMarshaller,
            StreamingOutputCallResponseMarshaller);

        static readonly Method<StreamingInputCallRequest, StreamingInputCallResponse> StreamingInputCallMethod = new Method<StreamingInputCallRequest, StreamingInputCallResponse>(
            MethodType.ClientStreaming,
            "StreamingInputCall",
            StreamingInputCallRequestMarshaller,
            StreamingInputCallResponseMarshaller);

        static readonly Method<StreamingOutputCallRequest, StreamingOutputCallResponse> FullDuplexCallMethod = new Method<StreamingOutputCallRequest, StreamingOutputCallResponse>(
            MethodType.DuplexStreaming,
            "FullDuplexCall",
            StreamingOutputCallRequestMarshaller,
            StreamingOutputCallResponseMarshaller);

        static readonly Method<StreamingOutputCallRequest, StreamingOutputCallResponse> HalfDuplexCallMethod = new Method<StreamingOutputCallRequest, StreamingOutputCallResponse>(
            MethodType.DuplexStreaming,
            "HalfDuplexCall",
            StreamingOutputCallRequestMarshaller,
            StreamingOutputCallResponseMarshaller);

        public interface ITestServiceClient
        {
            Empty EmptyCall(Empty request, CancellationToken token = default(CancellationToken));

            Task<Empty> EmptyCallAsync(Empty request, CancellationToken token = default(CancellationToken));

            SimpleResponse UnaryCall(SimpleRequest request, CancellationToken token = default(CancellationToken));

            Task<SimpleResponse> UnaryCallAsync(SimpleRequest request, CancellationToken token = default(CancellationToken));

            void StreamingOutputCall(StreamingOutputCallRequest request, IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken));

            ClientStreamingAsyncResult<StreamingInputCallRequest, StreamingInputCallResponse> StreamingInputCall(CancellationToken token = default(CancellationToken));

            IObserver<StreamingOutputCallRequest> FullDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken));

            IObserver<StreamingOutputCallRequest> HalfDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken));
        }

        public class TestServiceClientStub : AbstractStub<TestServiceClientStub, StubConfiguration>, ITestServiceClient
        {
            public TestServiceClientStub(Channel channel) : base(channel, StubConfiguration.Default)
            {
            }

            public TestServiceClientStub(Channel channel, StubConfiguration config) : base(channel, config)
            {
            }

            public Empty EmptyCall(Empty request, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, EmptyCallMethod);
                return Calls.BlockingUnaryCall(call, request, token);
            }

            public Task<Empty> EmptyCallAsync(Empty request, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, EmptyCallMethod);
                return Calls.AsyncUnaryCall(call, request, token);
            }

            public SimpleResponse UnaryCall(SimpleRequest request, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, UnaryCallMethod);
                return Calls.BlockingUnaryCall(call, request, token);
            }

            public Task<SimpleResponse> UnaryCallAsync(SimpleRequest request, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, UnaryCallMethod);
                return Calls.AsyncUnaryCall(call, request, token);
            }

            public void StreamingOutputCall(StreamingOutputCallRequest request, IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, StreamingOutputCallMethod);
                Calls.AsyncServerStreamingCall(call, request, responseObserver, token);
            }

            public ClientStreamingAsyncResult<StreamingInputCallRequest, StreamingInputCallResponse> StreamingInputCall(CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, StreamingInputCallMethod);
                return Calls.AsyncClientStreamingCall(call, token);
            }

            public IObserver<StreamingOutputCallRequest> FullDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, FullDuplexCallMethod);
                return Calls.DuplexStreamingCall(call, responseObserver, token);
            }

            public IObserver<StreamingOutputCallRequest> HalfDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken))
            {
                var call = CreateCall(ServiceName, HalfDuplexCallMethod);
                return Calls.DuplexStreamingCall(call, responseObserver, token);
            }
        }

        // server-side interface
        public interface ITestService
        {
            void EmptyCall(Empty request, IObserver<Empty> responseObserver);

            void UnaryCall(SimpleRequest request, IObserver<SimpleResponse> responseObserver);

            void StreamingOutputCall(StreamingOutputCallRequest request, IObserver<StreamingOutputCallResponse> responseObserver);

            IObserver<StreamingInputCallRequest> StreamingInputCall(IObserver<StreamingInputCallResponse> responseObserver);

            IObserver<StreamingOutputCallRequest> FullDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver);

            IObserver<StreamingOutputCallRequest> HalfDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver);
        }

        public static ServerServiceDefinition BindService(ITestService serviceImpl)
        {
            return ServerServiceDefinition.CreateBuilder(ServiceName)
                .AddMethod(EmptyCallMethod, serviceImpl.EmptyCall)
                .AddMethod(UnaryCallMethod, serviceImpl.UnaryCall)
                .AddMethod(StreamingOutputCallMethod, serviceImpl.StreamingOutputCall)
                .AddMethod(StreamingInputCallMethod, serviceImpl.StreamingInputCall)
                .AddMethod(FullDuplexCallMethod, serviceImpl.FullDuplexCall)
                .AddMethod(HalfDuplexCallMethod, serviceImpl.HalfDuplexCall)
                .Build();
        }

        public static ITestServiceClient NewStub(Channel channel)
        {
            return new TestServiceClientStub(channel);
        }
    }
}
