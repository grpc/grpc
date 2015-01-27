using System;
using Google.GRPC.Interop;
using System.Threading.Tasks;

namespace Google.GRPC.Core
{
	public class StreamingOutputObservable<TResponse> : IObservable<TResponse>
	{
		readonly ICallContext ctx;
		// only one observer allowed currently
		IObserver<TResponse> onlyObserver;
		Func<byte[], TResponse> demarshaller;

		public StreamingOutputObservable(ICallContext ctx, Func<byte[], TResponse> demarshaller)
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

            try {
			    while (true)
			    {
				    byte[] payload = ctx.Read();
				    if (payload == null)
				    {
					    break;
				    }
				    TResponse response = demarshaller(payload);
				    
                    // TODO: catch exceptions from user code...
                    onlyObserver.OnNext(response);
			    }

                Status status = ctx.Wait();
                // TODO: propagate error if status not OK.

                // TODO: catch exceptions from user code...
			    onlyObserver.OnCompleted();
            }
            finally {
                ctx.Dispose();
            }
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

