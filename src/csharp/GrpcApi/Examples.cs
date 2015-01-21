using System;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Linq;
using System.Reactive.Linq;

namespace Google.GRPC.Examples.Math
{

	public class Examples {

		private IMathServiceClient stub = new DummyMathServiceClient();

		public void DivExample()
		{
			DivReply result = stub.Div(new DivArgs { Dividend = 4, Divisor = 5 });
			Console.WriteLine(result);
		}

		public void DivAsyncExample()
		{
			Task<DivReply> call = stub.DivAsync(new DivArgs { Dividend = 4, Divisor = 5 });
			DivReply result = call.Result;
			Console.WriteLine(result);
		}

		public void DivAsyncWithCancellationExample()
		{
			Task<DivReply> call = stub.DivAsync(new DivArgs { Dividend = 4, Divisor = 5 });
			DivReply result = call.Result;
			Console.WriteLine(result);
		}

		public void FibExample()
		{
			IObservable<Number> response = stub.Fib(new FibArgs { Limit = 5 });

			// .ToList() requires reactive Linq
			IList<Number> numbers = response.ToList().Wait();
			Console.WriteLine(numbers);
		}

		public void SumExample()
		{
			// Cancellation options:
			// -- inputs.OnError() can be used to cancel
			// -- add CancellationToken as a parameter
			IObserver<Number> numbers;
			Task<Number> call = stub.Sum(out numbers);
			numbers.OnNext(new Number{Num = 1});
			numbers.OnNext(new Number{Num = 2});
			numbers.OnNext(new Number{Num = 3});
			numbers.OnCompleted();

			Console.WriteLine(call.Result);
		}

		public void DivManyExample()
		{
			IObserver<DivArgs> requestSink;
			IObservable<DivReply> response = stub.DivMany(out requestSink);
			requestSink.OnNext(new DivArgs {Dividend = 5, Divisor = 4});
			requestSink.OnNext(new DivArgs { Dividend = 3, Divisor = 2 });
			requestSink.OnNext(new DivArgs { Dividend = 6, Divisor = 2 });
			requestSink.OnCompleted();

			Console.WriteLine(response.ToList().Wait());
		}

		public void DependendRequestsExample()
		{
			var numberList = new List<Number> { new Number { Num = 1 }, 
				new Number { Num = 2 }, new Number { Num = 3 } };

			numberList.ToObservable();

			IObserver<Number> numbers;
			Task<Number> call = stub.Sum(out numbers);            
			foreach(var num in numberList) {
				numbers.OnNext(num);
			}
			numbers.OnCompleted();

			Number sum = call.Result;

			DivReply result = stub.Div(new DivArgs { Dividend = sum.Num, Divisor = numberList.Count });
		}

		public void Experiment()
		{
			var numberList = new List<Number> { new Number { Num = 1 }, 
				new Number { Num = 2 }, new Number { Num = 3 } };

			IObservable<Number> observable = numberList.ToObservable();

			IObserver<Number> observer = null;
			IDisposable subscription = observable.Subscribe(observer);

			subscription.Dispose();


		}
	}
}

