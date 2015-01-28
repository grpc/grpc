using System;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Reactive.Linq;
using Google.GRPC.Wrappers;
using Google.GRPC.Core;

namespace math
{
	/// <summary>
	/// Implementation of math service stub (this is handwritten version of code 
	/// that will normally be generated).
	/// </summary>
	public class MathServiceClientImpl : IMathServiceClient
	{
		readonly Channel channel;

		public MathServiceClientImpl(Channel channel)
		{
			this.channel = channel;
		}

		public DivReply Div(DivArgs args, CancellationToken token = default(CancellationToken))
		{
            var call = new Google.GRPC.Core.Call<DivArgs, DivReply>("/math.Math/Div", Serialize_DivArgs, Deserialize_DivReply, channel);
            return Calls.BlockingUnaryCall(call, args, token);
		}

		public Task<DivReply> DivAsync(DivArgs args, CancellationToken token = default(CancellationToken))
		{
            var call = new Google.GRPC.Core.Call<DivArgs, DivReply>("/math.Math/Div", Serialize_DivArgs, Deserialize_DivReply, channel);
            return Calls.AsyncUnaryCall(call, args, token);
		}

		public IObservable<Num> Fib(FibArgs args, CancellationToken token = default(CancellationToken))
		{
            var call = new Google.GRPC.Core.Call<FibArgs, Num>("/math.Math/Fib", Serialize_FibArgs, Deserialize_Num, channel);
            return Calls.AsyncServerStreamingCall(call, args, token);
		}

		public Task<Num> Sum(IObservable<Num> inputs, CancellationToken token = default(CancellationToken))
		{
            var call = new Google.GRPC.Core.Call<Num, Num>("/math.Math/Sum", Serialize_Num, Deserialize_Num, channel);
            return Calls.AsyncClientStreamingCall(call, inputs, token);
		}

		public IObservable<DivReply> DivMany(IObservable<DivArgs> inputs, CancellationToken token = default(CancellationToken))
		{
            var call = new Google.GRPC.Core.Call<DivArgs, DivReply>("/math.Math/DivMany", Serialize_DivArgs, Deserialize_DivReply, channel);
            return Calls.DuplexStreamingCall(call, inputs, token);
		}

        private static byte[] Serialize_DivArgs(DivArgs arg) {
            return arg.ToByteArray();
        }

        private static byte[] Serialize_FibArgs(FibArgs arg) {
            return arg.ToByteArray();
        }

        private static byte[] Serialize_Num(Num arg) {
            return arg.ToByteArray();
        }

        private static DivReply Deserialize_DivReply(byte[] payload) {
            return DivReply.CreateBuilder().MergeFrom(payload).Build();
        }

        private static Num Deserialize_Num(byte[] payload) {
            return Num.CreateBuilder().MergeFrom(payload).Build();
        }
	}
}