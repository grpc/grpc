using System;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core.Internal
{
    internal class ServerWritingObserver<TWrite, TRead> : IObserver<TWrite>
	{
        readonly AsyncCall<TWrite, TRead> call;

        public ServerWritingObserver(AsyncCall<TWrite, TRead> call)
		{
            this.call = call;
		}

		public void OnCompleted()
		{
            // TODO: how bad is the Wait here?
            call.WriteStatusAsync(new Status(StatusCode.GRPC_STATUS_OK, "")).Wait();
		}

		public void OnError(Exception error)
		{
            // TODO: handle this...
			throw new InvalidOperationException("This should never be called.");
		}

		public void OnNext(TWrite value)
		{
            // TODO: how bad is the Wait here?
            call.WriteAsync(value).Wait();
		}
	}
}

