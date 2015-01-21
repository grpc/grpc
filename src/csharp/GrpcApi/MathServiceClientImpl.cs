using System;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Reactive.Linq;
using Google.GRPC.Interop;
using math;

namespace Google.GRPC.Examples.Math
{

	/// <summary>
	/// Implementation of math service stub (this is handwritten version of code 
	/// that will normally be generated).
	/// </summary>
	internal class MathServiceClientImpl : IMathServiceClient
	{
		readonly Channel channel;

		public MathServiceClientImpl(Channel channel) {
			this.channel = channel;
		}

		// TODO: how about a deadline for simple sync call?

		// statuses different from OK are thrown as an exception....
		public DivReply Div(DivArgs args, CancellationToken token = default(CancellationToken))
		{
			throw new NotImplementedException();
		}

		// statuses different from OK are thrown as an exception....
		public Task<DivReply> DivAsync(DivArgs args, CancellationToken token = default(CancellationToken))
		{
			throw new NotImplementedException();
		}

		public IObservable<Num> Fib(FibArgs args, CancellationToken token = default(CancellationToken))
		{
			throw new NotImplementedException();
		}

		public Task<Num> Sum(out IObserver<Num> inputs, CancellationToken token = default(CancellationToken))
		{
			throw new NotImplementedException();
		}

		public IObservable<DivReply> DivMany(out IObserver<DivArgs> inputs, CancellationToken token = default(CancellationToken))
		{
			throw new NotImplementedException();
		}
	}
}