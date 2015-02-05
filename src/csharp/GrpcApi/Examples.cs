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
            var recorder = new RecordingObserver<Num>();
            stub.Fib(new FibArgs.Builder { Limit = 5 }.Build(), recorder);

			List<Num> numbers = recorder.ToList().Result;
            Console.WriteLine("Fib Result: " + string.Join("|", recorder.ToList().Result));
		}

		public static void SumExample(IMathServiceClient stub)
		{
			List<Num> numbers = new List<Num>{new Num.Builder { Num_ = 1 }.Build(), 
				new Num.Builder { Num_ = 2 }.Build(),
				new Num.Builder { Num_ = 3 }.Build()};
			
            var res = stub.Sum();
            foreach (var num in numbers) {
                res.Inputs.OnNext(num);
            }
            res.Inputs.OnCompleted();

			Console.WriteLine("Sum Result: " + res.Task.Result);
		}

		public static void DivManyExample(IMathServiceClient stub)
		{
			List<DivArgs> divArgsList = new List<DivArgs>{
				new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build(),
				new DivArgs.Builder { Dividend = 100, Divisor = 21 }.Build(),
				new DivArgs.Builder { Dividend = 7, Divisor = 2 }.Build()
			};

            var recorder = new RecordingObserver<DivReply>();
			
            var inputs = stub.DivMany(recorder);
            foreach (var input in divArgsList)
            {
                inputs.OnNext(input);
            }
            inputs.OnCompleted();

			Console.WriteLine("DivMany Result: " + string.Join("|", recorder.ToList().Result));
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

