using System;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Reactive.Linq;
using Google.GRPC.Interop;
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

		// TODO: add deadline to the API
		public DivReply Div(DivArgs args, CancellationToken token = default(CancellationToken))
		{
			byte[] result;
			Status status = channel.SimpleBlockingCall("/math.Math/Div", args.ToByteArray(), out result, Timespec.InfFuture, token);
			if (status.StatusCode != StatusCode.GRPC_STATUS_OK)
			{
				throw new RpcException(status);
			}
			return DivReply.CreateBuilder().MergeFrom(result).Build();
		}

		public Task<DivReply> DivAsync(DivArgs args, CancellationToken token = default(CancellationToken))
		{
			throw new NotImplementedException();
		}

		public IObservable<Num> Fib(FibArgs args, CancellationToken token = default(CancellationToken))
		{


			// FIXME: this is not going to work with IObservable as a result!!!!
			// TODO: implement IObservable to wait for the streaming output...

			throw new NotImplementedException();
		}

		public async Task<Num> Sum(IObservable<Num> inputs, CancellationToken token = default(CancellationToken))
		{
			// TODO: cancellation support
			// TODO: timeout support

			CallContext ctx = channel.StreamingCall("/math.Math/Sum", Timespec.InfFuture);

			ctx.Start(false);

			// TODO: dispose the subscription....
			IDisposable subscription = inputs.Subscribe(new StreamingInputObserver<Num>(ctx));

			// TODO: don't create task, but make this method async. Then we can use using to cleanup the context....

			return await Task<Num>.Factory.StartNew(
				() => {
				// TODO: the read and wait part is the same as for simple call
				byte[] response = ctx.Read();
				Status s = ctx.Wait();
				// TODO: figure out a good way how to dispose ctx (even when errors happen...)
				ctx.Dispose();  
				return Num.CreateBuilder().MergeFrom(response).Build();
			}
			);
		}

		public IObservable<DivReply> DivMany(IObservable<DivArgs> inputs, CancellationToken token = default(CancellationToken))
		{
			// TODO: timeout support
			CallContext ctx = channel.StreamingCall("/math.Math/DivMany", Timespec.InfFuture);
			ctx.Start(false);

			// TODO: dispose the subscription....
			inputs.Subscribe(new StreamingInputObserver<DivArgs>(ctx));

			return new StreamingOutputObservable<DivReply>(ctx, (payload) => DivReply.CreateBuilder().MergeFrom(payload).Build());
		}
	}
}