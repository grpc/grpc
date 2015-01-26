using System;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Reactive.Linq;

namespace math
{
	public class Examples
	{
		public static void DivExample(IMathServiceClient stub)
		{
			DivReply result = stub.Div(new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build());
			Console.WriteLine("Div Result: " + result);
		}

		public static void DivAsyncExample(IMathServiceClient stub)
		{
			Task<DivReply> call = stub.DivAsync(new DivArgs.Builder { Dividend = 4, Divisor = 5 }.Build());
			DivReply result = call.Result;
			Console.WriteLine(result);
		}

		public static void DivAsyncWithCancellationExample(IMathServiceClient stub)
		{
			Task<DivReply> call = stub.DivAsync(new DivArgs.Builder { Dividend = 4, Divisor = 5 }.Build());
			DivReply result = call.Result;
			Console.WriteLine(result);
		}

		public static void FibExample(IMathServiceClient stub)
		{
			IObservable<Num> response = stub.Fib(new FibArgs.Builder { Limit = 5 }.Build());

			// .ToList() requires reactive Linq
			IList<Num> numbers = response.ToList().Wait();
			Console.WriteLine(numbers);
		}

		public static void SumExample(IMathServiceClient stub)
		{
			// Cancellation options:
			// -- inputs.OnError() can be used to cancel
			// -- add CancellationToken as a parameter
			//IObserver<Num> numbers;
			List<Num> numbers = new List<Num>{new Num.Builder { Num_ = 1 }.Build(), 
				new Num.Builder { Num_ = 2 }.Build(),
				new Num.Builder { Num_ = 3 }.Build()};
			Task<Num> call = stub.Sum(numbers.ToObservable());


			Console.WriteLine("Sum Result: " + call.Result);
		}

		public static void DivManyExample(IMathServiceClient stub)
		{
			List<DivArgs> divArgsList = new List<DivArgs>{
				new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build(),
				new DivArgs.Builder { Dividend = 100, Divisor = 21 }.Build(),
				new DivArgs.Builder { Dividend = 7, Divisor = 2 }.Build()
			};
			IObservable<DivReply> response = stub.DivMany(divArgsList.ToObservable());

			Console.WriteLine("DivMany Result: " + string.Join("|", response.ToList().Wait()));
		}

		public static void DependendRequestsExample(IMathServiceClient stub)
		{
			var numberList = new List<Num>
			{ new Num.Builder{ Num_ = 1 }.Build(), 
				new Num.Builder{ Num_ = 2 }.Build(), new Num.Builder{ Num_ = 3 }.Build()
			};

			numberList.ToObservable();

			//IObserver<Num> numbers;
			//Task<Num> call = stub.Sum(out numbers);            
			//foreach (var num in numberList)
			//{
			//	numbers.OnNext(num);
			//}
			//numbers.OnCompleted();

			//Num sum = call.Result;

			//DivReply result = stub.Div(new DivArgs.Builder { Dividend = sum.Num_, Divisor = numberList.Count }.Build());
		}
	}
}

