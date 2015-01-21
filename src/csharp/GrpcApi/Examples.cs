using System;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Linq;
using System.Reactive.Linq;
using math;

namespace Google.GRPC.Examples.Math
{

	public class Examples {

		private IMathServiceClient stub = new DummyMathServiceClient();

		public void DivExample()
		{
			DivReply result = stub.Div(new DivArgs.Builder { Dividend = 4, Divisor = 5 }.Build());
			Console.WriteLine(result);
		}

		public void DivAsyncExample()
		{
			Task<DivReply> call = stub.DivAsync(new DivArgs.Builder { Dividend = 4, Divisor = 5 }.Build());
			DivReply result = call.Result;
			Console.WriteLine(result);
		}

		public void DivAsyncWithCancellationExample()
		{
			Task<DivReply> call = stub.DivAsync(new DivArgs.Builder { Dividend = 4, Divisor = 5 }.Build());
			DivReply result = call.Result;
			Console.WriteLine(result);
		}

		public void FibExample()
		{
			IObservable<Num> response = stub.Fib(new FibArgs.Builder{ Limit = 5 }.Build());

			// .ToList() requires reactive Linq
			IList<Num> numbers = response.ToList().Wait();
			Console.WriteLine(numbers);
		}

		public void SumExample()
		{
			// Cancellation options:
			// -- inputs.OnError() can be used to cancel
			// -- add CancellationToken as a parameter
			IObserver<Num> numbers;
			Task<Num> call = stub.Sum(out numbers);
			numbers.OnNext(new Num.Builder{Num_ = 1}.Build());
			numbers.OnNext(new Num.Builder{Num_ = 2}.Build());
			numbers.OnNext(new Num.Builder{Num_ = 3}.Build());
			numbers.OnCompleted();

			Console.WriteLine(call.Result);
		}

		public void DivManyExample()
		{
			IObserver<DivArgs> requestSink;
			IObservable<DivReply> response = stub.DivMany(out requestSink);
			requestSink.OnNext(new DivArgs.Builder{Dividend = 5, Divisor = 4}.Build());
			requestSink.OnNext(new DivArgs.Builder{ Dividend = 3, Divisor = 2 }.Build());
			requestSink.OnNext(new DivArgs.Builder{ Dividend = 6, Divisor = 2 }.Build());
			requestSink.OnCompleted();

			Console.WriteLine(response.ToList().Wait());
		}

		public void DependendRequestsExample()
		{
			var numberList = new List<Num> { new Num.Builder{ Num_ = 1 }.Build(), 
				new Num.Builder{ Num_ = 2 }.Build(), new Num.Builder{ Num_ = 3 }.Build() };

			numberList.ToObservable();

			IObserver<Num> numbers;
			Task<Num> call = stub.Sum(out numbers);            
			foreach(var num in numberList) {
				numbers.OnNext(num);
			}
			numbers.OnCompleted();

			Num sum = call.Result;

			DivReply result = stub.Div(new DivArgs.Builder{ Dividend = sum.Num_, Divisor = numberList.Count }.Build());
		}

		public void Experiment()
		{
			var numberList = new List<Num> { new Num.Builder{ Num_ = 1 }.Build(), 
				new Num.Builder{ Num_ = 2}.Build(), new Num.Builder{Num_ = 3}.Build() };

			IObservable<Num> observable = numberList.ToObservable();

			IObserver<Num> observer = null;
			IDisposable subscription = observable.Subscribe(observer);

			subscription.Dispose();


		}
	}
}

