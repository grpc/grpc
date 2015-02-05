using System;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Reactive.Linq;

namespace math
{
//	/// <summary>
//	/// Dummy local implementation of math service.
//	/// </summary>
//	public class DummyMathServiceClient : IMathServiceClient
//	{
//		public DivReply Div(DivArgs args, CancellationToken token = default(CancellationToken))
//		{
//			// TODO: cancellation...
//			return DivInternal(args);
//		}
//		
//		public Task<DivReply> DivAsync(DivArgs args, CancellationToken token = default(CancellationToken))
//		{
//			return Task.Factory.StartNew(() => DivInternal(args), token);
//		}
//
//		public IObservable<Num> Fib(FibArgs args, CancellationToken token = default(CancellationToken))
//		{
//			if (args.Limit > 0)
//			{
//				// TODO: cancellation
//				return FibInternal(args.Limit).ToObservable();
//			}
//
//			throw new NotImplementedException("Not implemented yet");
//		}
//
//		public Task<Num> Sum(IObservable<Num> inputs, CancellationToken token = default(CancellationToken))
//		{
//			// TODO: implement
//			inputs = null;
//			return Task.Factory.StartNew(() => Num.CreateBuilder().Build(), token);
//		}
//
//		public IObservable<DivReply> DivMany(IObservable<DivArgs> inputs, CancellationToken token = default(CancellationToken))
//		{
//			// TODO: implement
//			inputs = null;
//			return new List<DivReply> { }.ToObservable ();
//		}
//
//
//		DivReply DivInternal(DivArgs args)
//		{
//			long quotient = args.Dividend / args.Divisor;
//			long remainder = args.Dividend % args.Divisor;
//			return new DivReply.Builder{ Quotient = quotient, Remainder = remainder }.Build();
//		}
//
//		IEnumerable<Num> FibInternal(long n)
//		{
//			long a = 0;
//			yield return new Num.Builder{Num_=a}.Build();
//
//			long b = 1;
//			for (long i = 0; i < n - 1; i++)
//			{
//				long temp = a;
//				a = b;
//				b = temp + b;
//				yield return new Num.Builder{Num_=a}.Build();
//			}
//		}
//	}
}

