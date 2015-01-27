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
            // TODO: implement this. just return a task same as for Sum

			throw new NotImplementedException();
		}

		public IObservable<Num> Fib(FibArgs args, CancellationToken token = default(CancellationToken))
		{
            using (CallContext ctx = channel.CreateCall("/math.Math/Fib", Timespec.InfFuture))
            {
                ctx.Start(false);
               
                // TODO: this is the same code as for blocking call!!!
                ctx.Write(args.ToByteArray());
                ctx.WritesDone();

                return new StreamingOutputObservable<Num>(ctx.AddRef(), (payload) => Num.CreateBuilder().MergeFrom(payload).Build());
            }
		}

		public Task<Num> Sum(IObservable<Num> inputs, CancellationToken token = default(CancellationToken))
		{
			// TODO: cancellation support
			// TODO: timeout support

            using (CallContext ctx = channel.CreateCall("/math.Math/Sum", Timespec.InfFuture))
            {
                ctx.Start(false);

                // TODO: dispose the subscription....
                IDisposable subscription = inputs.Subscribe(new StreamingInputObserver<Num>(ctx.AddRef()));

                // TODO: don't create task, but make this method async. Then we can use using to cleanup the context....

                ICallContext ctxRef = ctx.AddRef();

                // TODO: factor this out!!!
                return Task<Num>.Factory.StartNew(
                    () => {
                        try
                        {
                            // TODO: the read and wait part is the same as for simple call
                            byte[] response = ctxRef.Read();
                            Status s = ctxRef.Wait();

                            // TOOD: throw if status not OK.
                            // TODO: figure out a good way how to dispose ctx (even when errors happen...)
                            return Num.CreateBuilder().MergeFrom(response).Build();
                        } 
                        finally
                        {
                            ctxRef.Dispose();
                        }
                });
            }

		}

		public IObservable<DivReply> DivMany(IObservable<DivArgs> inputs, CancellationToken token = default(CancellationToken))
		{
			// TODO: timeout support
            using (CallContext ctx = channel.CreateCall("/math.Math/DivMany", Timespec.InfFuture))
            {
                ctx.Start(false);

                // TODO: dispose the subscription....
                IDisposable inputSubscription = inputs.Subscribe(new StreamingInputObserver<DivArgs>(ctx.AddRef()));

                return new StreamingOutputObservable<DivReply>(ctx.AddRef(), (payload) => DivReply.CreateBuilder().MergeFrom(payload).Build());
            }
		}
	}
}