using System;
using Google.GRPC.Interop;

namespace Google.GRPC.Core
{
	public class StreamingInputObserver<TRequest> : IObserver<TRequest>
		where TRequest : Google.ProtocolBuffers.IMessage
	{

		readonly CallContext ctx;

		public StreamingInputObserver(CallContext ctx)
		{
			this.ctx = ctx;
		}

		public void OnCompleted()
		{
			//TODO: do some checks..
			ctx.WritesDone();
		}

		public void OnError(Exception error)
		{
			throw new InvalidOperationException("This should never be called.");
		}

		public void OnNext(TRequest value)
		{
			// TODO: do some checks....
			// TODO: check serialization result...
			ctx.Write(value.ToByteArray());
		}
	}
}

