using System;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Reactive.Linq;

namespace math
{
	/// <summary>
	/// Hand-written stub for MathService defined in math.proto.
	/// This code will be generated by gRPC codegen in the future.
	/// </summary>
	public interface IMathServiceClient
	{
		DivReply Div(DivArgs args, CancellationToken token = default(CancellationToken));

		Task<DivReply> DivAsync(DivArgs args, CancellationToken token = default(CancellationToken));

		IObservable<Num> Fib(FibArgs args, CancellationToken token = default(CancellationToken));

		Task<Num> Sum(IObservable<Num> inputs, CancellationToken token = default(CancellationToken));

		IObservable<DivReply> DivMany(IObservable<DivArgs> inputs, CancellationToken token = default(CancellationToken));
	}
}