using System;
using Google.GRPC.Wrappers;

namespace Google.GRPC.Core
{
	public class StreamingInputObserver<TRequest> : IObserver<TRequest>
		where TRequest : Google.ProtocolBuffers.IMessage
	{
		readonly ICallContext ctx;
        bool writingFinished = false;

		public StreamingInputObserver(ICallContext ctx)
		{
			this.ctx = ctx;
		}

		public void OnCompleted()
		{
            checkWritingNotFinished();

			ctx.WritesDone();
            writingFinished = true;
         
            ctx.Dispose();
		}

		public void OnError(Exception error)
		{
			throw new InvalidOperationException("This should never be called.");
		}

		public void OnNext(TRequest value)
		{
            checkWritingNotFinished();

			// TODO: do some checks....
			// TODO: check serialization result...
			ctx.Write(value.ToByteArray());
		}

        private void checkWritingNotFinished() {
            if (writingFinished)
            {
                // TODO: throw something more meaningful
                throw new InvalidOperationException("Writing already finished");
            }
        }
	}
}

