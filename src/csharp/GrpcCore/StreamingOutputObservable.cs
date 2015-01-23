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

			return null; // TODO: return unsubscriber...
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
			ctx.Dispose();

			onlyObserver.OnCompleted();

			//TODO: how to read the finished status???
		}
		//		private class Unsubscriber : IDisposable
		//		{
		//			private List<IObserver<Location>>_observers;
		//			private IObserver<Location> _observer;
		//
		//			public Unsubscriber(List<IObserver<Location>> observers, IObserver<Location> observer)
		//			{
		//				this._observers = observers;
		//				this._observer = observer;
		//			}
		//
		//			public void Dispose()
		//			{
		//				if (_observer != null && _observers.Contains(_observer))
		//					_observers.Remove(_observer);
		//			}
		//		}
	}
}

