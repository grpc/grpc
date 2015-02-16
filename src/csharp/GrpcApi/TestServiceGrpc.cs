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
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Reactive.Linq;
using Google.GRPC.Core;

namespace grpc.testing
{
    /// <summary>
    /// TestService (this is handwritten version of code that will normally be generated).
    /// </summary>
    public class TestServiceGrpc
    {
        readonly static Marshaller<Empty> emptyMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), Empty.ParseFrom);
        readonly static Marshaller<SimpleRequest> simpleRequestMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), SimpleRequest.ParseFrom);
        readonly static Marshaller<SimpleResponse> simpleResponseMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), SimpleResponse.ParseFrom);
        readonly static Marshaller<StreamingOutputCallRequest> streamingOutputCallRequestMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), StreamingOutputCallRequest.ParseFrom);
        readonly static Marshaller<StreamingOutputCallResponse> streamingOutputCallResponseMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), StreamingOutputCallResponse.ParseFrom);
        readonly static Marshaller<StreamingInputCallRequest> streamingInputCallRequestMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), StreamingInputCallRequest.ParseFrom);
        readonly static Marshaller<StreamingInputCallResponse> streamingInputCallResponseMarshaller = Marshallers.Create((arg) => arg.ToByteArray(), StreamingInputCallResponse.ParseFrom);

        readonly static Method<Empty, Empty> emptyCallMethod = new Method<Empty, Empty>(
            MethodType.Unary,
            "/grpc.testing.TestService/EmptyCall",
            emptyMarshaller,
            emptyMarshaller
        );
        readonly static Method<SimpleRequest, SimpleResponse> unaryCallMethod = new Method<SimpleRequest, SimpleResponse>(
            MethodType.Unary,
            "/grpc.testing.TestService/UnaryCall",
            simpleRequestMarshaller,
            simpleResponseMarshaller
        );
        readonly static Method<StreamingOutputCallRequest, StreamingOutputCallResponse> streamingOutputCallMethod = new Method<StreamingOutputCallRequest, StreamingOutputCallResponse>(
            MethodType.ServerStreaming,
            "/grpc.testing.TestService/StreamingOutputCall",
            streamingOutputCallRequestMarshaller,
            streamingOutputCallResponseMarshaller
            );
        readonly static Method<StreamingInputCallRequest, StreamingInputCallResponse> streamingInputCallMethod = new Method<StreamingInputCallRequest, StreamingInputCallResponse>(
            MethodType.ClientStreaming,
            "/grpc.testing.TestService/StreamingInputCall",
            streamingInputCallRequestMarshaller,
            streamingInputCallResponseMarshaller
            );
        readonly static Method<StreamingOutputCallRequest, StreamingOutputCallResponse> fullDuplexCallMethod = new Method<StreamingOutputCallRequest, StreamingOutputCallResponse>(
            MethodType.DuplexStreaming,
            "/grpc.testing.TestService/FullDuplexCall",
            streamingOutputCallRequestMarshaller,
            streamingOutputCallResponseMarshaller
            );
        readonly static Method<StreamingOutputCallRequest, StreamingOutputCallResponse> halfDuplexCallMethod = new Method<StreamingOutputCallRequest, StreamingOutputCallResponse>(
            MethodType.DuplexStreaming,
            "/grpc.testing.TestService/HalfDuplexCall",
            streamingOutputCallRequestMarshaller,
            streamingOutputCallResponseMarshaller
            );

        public interface ITestServiceClient
        {
            Empty EmptyCall(Empty request, CancellationToken token = default(CancellationToken));

            Task<Empty> EmptyCallAsync(Empty request, CancellationToken token = default(CancellationToken));

            SimpleResponse UnaryCall(SimpleRequest request, CancellationToken token = default(CancellationToken));

            Task<SimpleResponse> UnaryCallAsync(SimpleRequest request, CancellationToken token = default(CancellationToken));

            Task StreamingOutputCall(StreamingOutputCallRequest request, IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken));

            ClientStreamingAsyncResult<StreamingInputCallRequest, StreamingInputCallResponse> StreamingInputCall(CancellationToken token = default(CancellationToken));

            IObserver<StreamingOutputCallRequest> FullDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken));

            IObserver<StreamingOutputCallRequest> HalfDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken));
        }

        public class TestServiceClientStub : ITestServiceClient
        {
            readonly Channel channel;

            public TestServiceClientStub(Channel channel)
            {
                this.channel = channel;
            }

            public Empty EmptyCall(Empty request, CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<Empty, Empty>(emptyCallMethod, channel);
                return Calls.BlockingUnaryCall(call, request, token);
            }

            public Task<Empty> EmptyCallAsync(Empty request, CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<Empty, Empty>(emptyCallMethod, channel);
                return Calls.AsyncUnaryCall(call, request, token);
            }

            public SimpleResponse UnaryCall(SimpleRequest request, CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<SimpleRequest, SimpleResponse>(unaryCallMethod, channel);
                return Calls.BlockingUnaryCall(call, request, token);
            }

            public Task<SimpleResponse> UnaryCallAsync(SimpleRequest request, CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<SimpleRequest, SimpleResponse>(unaryCallMethod, channel);
                return Calls.AsyncUnaryCall(call, request, token);
            }

            public Task StreamingOutputCall(StreamingOutputCallRequest request, IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken)) {
                var call = new Google.GRPC.Core.Call<StreamingOutputCallRequest, StreamingOutputCallResponse>(streamingOutputCallMethod, channel);
                return Calls.AsyncServerStreamingCall(call, request, responseObserver, token);
            }

            public ClientStreamingAsyncResult<StreamingInputCallRequest, StreamingInputCallResponse> StreamingInputCall(CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<StreamingInputCallRequest, StreamingInputCallResponse>(streamingInputCallMethod, channel);
                return Calls.AsyncClientStreamingCall(call, token);
            }

            public IObserver<StreamingOutputCallRequest> FullDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<StreamingOutputCallRequest, StreamingOutputCallResponse>(fullDuplexCallMethod, channel);
                return Calls.DuplexStreamingCall(call, responseObserver, token);
            }


            public IObserver<StreamingOutputCallRequest> HalfDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver, CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<StreamingOutputCallRequest, StreamingOutputCallResponse>(halfDuplexCallMethod, channel);
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
            return ServerServiceDefinition.CreateBuilder("/grpc.testing.TestService/")
                .AddMethod(emptyCallMethod, serviceImpl.EmptyCall)
                .AddMethod(unaryCallMethod, serviceImpl.UnaryCall)
                .AddMethod(streamingOutputCallMethod, serviceImpl.StreamingOutputCall)
                .AddMethod(streamingInputCallMethod, serviceImpl.StreamingInputCall)
                .AddMethod(fullDuplexCallMethod, serviceImpl.FullDuplexCall)
                .AddMethod(halfDuplexCallMethod, serviceImpl.HalfDuplexCall)
                .Build();
        }

        public static ITestServiceClient NewStub(Channel channel)
        {
            return new TestServiceClientStub(channel);
        }
    }
}
