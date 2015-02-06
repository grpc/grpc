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

        public interface ITestServiceClient
        {
            Empty EmptyCall(Empty request, CancellationToken token = default(CancellationToken));

            Task<Empty> EmptyCallAsync(Empty request, CancellationToken token = default(CancellationToken));

            SimpleResponse UnaryCall(SimpleRequest request, CancellationToken token = default(CancellationToken));

            Task<SimpleResponse> UnaryCallAsync(SimpleRequest request, CancellationToken token = default(CancellationToken));

            // TODO: StreamingOutputCall
            // TODO: StreamingInputCall
            // TODO: FullDuplexCall
            // TODO: HalfDuplexCall
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
        }

        // server-side interface
        public interface ITestService
        {
            void EmptyCall(Empty request, IObserver<Empty> responseObserver);

            void UnaryCall(SimpleRequest request, IObserver<SimpleResponse> responseObserver);
        }

        public static ServerServiceDefinition BindService(ITestService serviceImpl)
        {
            return ServerServiceDefinition.CreateBuilder("/grpc.testing.TestService/")
                .AddMethod(emptyCallMethod, serviceImpl.EmptyCall)
                .AddMethod(unaryCallMethod, serviceImpl.UnaryCall)
                .Build();
        }

        public static ITestServiceClient NewStub(Channel channel)
        {
            return new TestServiceClientStub(channel);
        }
    }
}