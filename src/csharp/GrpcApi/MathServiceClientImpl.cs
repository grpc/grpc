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

		// TODO: how to represent a deadline for simple sync call?
		// statuses different from OK are thrown as an exception....
		public DivReply Div(DivArgs args, CancellationToken token = default(CancellationToken))
		{
			//TODO: throwing exceptions?

			// TODO: implement cancellation
			byte[] result;
			Status status = channel.SimpleBlockingCall("/math.Math/Div", args.ToByteArray(), out result, GPRTimespec.InfFuture);
			// TODO: only parse data if corresponding status is returned..
			// TODO: only parse data if result is not null...
			return DivReply.CreateBuilder().MergeFrom(result).Build();
		}
		// statuses different from OK are thrown as an exception....
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

		public void Fib2(FibArgs args, IObserver<Num> output, CancellationToken token = default(CancellationToken))
		{


			// FIXME: this is not going to work with IObservable as a result!!!!
			// TODO: implement IObservable to wait for the streaming output...

			throw new NotImplementedException();
		}

		public Task<Num> Sum(out IObserver<Num> inputs, CancellationToken token = default(CancellationToken))
		{
			// TODO: timeout support
			CallContext ctx = channel.StreamingCall("/math.Math/Sum", GPRTimespec.InfFuture);

			ctx.Start(false);

			// or we can subscribe this if inputs is IObservable instead.
			inputs = new StreamingInputObserver<Num>(ctx);

			// TODO: don't create task, but make this method async. Then we can use using to cleanup the context....

			return Task<Num>.Factory.StartNew(
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

		public IObservable<DivReply> DivMany(out IObserver<DivArgs> inputs, CancellationToken token = default(CancellationToken))
		{
			// TODO: timeout support
			CallContext ctx = channel.StreamingCall("/math.Math/DivMany", GPRTimespec.InfFuture);
			ctx.Start(false);
			
			inputs = new StreamingInputObserver<DivArgs>(ctx);

			return new StreamingOutputObservable<DivReply>(ctx, (payload) => DivReply.CreateBuilder().MergeFrom(payload).Build());
		}
	}
}