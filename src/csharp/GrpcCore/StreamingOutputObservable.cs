using System;
using Google.GRPC.Interop;
using System.Threading.Tasks;

namespace Google.GRPC.Core
{
	public class StreamingOutputObservable<TResponse> : IObservable<TResponse>
	{
		readonly CallContext ctx;
		// only one observer allowed currently
		IObserver<TResponse> onlyObserver;
		Func<byte[], TResponse> demarshaller;

		public StreamingOutputObservable(CallContext ctx, Func<byte[], TResponse> demarshaller)
		{
			this.ctx = ctx;
			this.demarshaller = demarshaller;
		}

		public IDisposable Subscribe(IObserver<TResponse> observer)
		{
			if (this.onlyObserver != null)
			{
				throw new InvalidOperationException("Only one observer allowed at a time");
			}

			this.onlyObserver = observer;

			Task.Factory.StartNew(ReaderTaskBody);
			// add task that reads the next response from the context....

			return new Unsubscriber();
		}

		private void ReaderTaskBody()
		{
			// TODO: reading should be split into more tasks to prevent deadlock

			while (true)
			{
				byte[] payload = ctx.Read();
				if (payload == null)
				{
					break;
				}
				TResponse response = demarshaller(payload);
				onlyObserver.OnNext(response);
			}


			ctx.Wait();
			//TODO: read the status and propagate error if needed.

			ctx.Dispose();

			onlyObserver.OnCompleted();
		}
			
		private class Unsubscriber : IDisposable
		{
			public void Dispose()
			{
				//TODO: implement this
			}
		}
	}
}

