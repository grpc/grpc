using System;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Reactive.Linq;
using Google.GRPC.Core;

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

            Task Fib(FibArgs request, IObserver<Num> responseObserver, CancellationToken token = default(CancellationToken));

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
                var call = new Google.GRPC.Core.Call<DivArgs, DivReply>(divMethod, channel);
                return Calls.BlockingUnaryCall(call, request, token);
            }

            public Task<DivReply> DivAsync(DivArgs request, CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<DivArgs, DivReply>(divMethod, channel);
                return Calls.AsyncUnaryCall(call, request, token);
            }

            public Task Fib(FibArgs request, IObserver<Num> responseObserver, CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<FibArgs, Num>(fibMethod, channel);
                return Calls.AsyncServerStreamingCall(call, request, responseObserver, token);
            }

            public ClientStreamingAsyncResult<Num, Num> Sum(CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<Num, Num>(sumMethod, channel);
                return Calls.AsyncClientStreamingCall(call, token);
            }

            public IObserver<DivArgs> DivMany(IObserver<DivReply> responseObserver, CancellationToken token = default(CancellationToken))
            {
                var call = new Google.GRPC.Core.Call<DivArgs, DivReply>(divManyMethod, channel);
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