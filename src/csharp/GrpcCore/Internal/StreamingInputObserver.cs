using System;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core.Internal
{
    internal class StreamingInputObserver<TWrite, TRead> : IObserver<TWrite>
	{
        readonly AsyncCall<TWrite, TRead> call;

        public StreamingInputObserver(AsyncCall<TWrite, TRead> call)
		{
            this.call = call;
		}

		public void OnCompleted()
		{
            // TODO: how bad is the Wait here?
            call.WritesCompletedAsync().Wait();
		}

		public void OnError(Exception error)
		{
			throw new InvalidOperationException("This should never be called.");
		}

		public void OnNext(TWrite value)
		{
            // TODO: how bad is the Wait here?
            call.WriteAsync(value).Wait();
		}
	}
}

