using System;
using System.Runtime.InteropServices;
using Google.GRPC.Interop;
using math;

namespace Google.GRPC.Demo
{
	class MainClass
	{
		private static void DoEmptyCall() {
			GRPCUtils.Init();

			using (Channel channel = new Channel("127.0.0.1:12345"))
			{
				byte[] result;
				Status status = channel.SimpleBlockingCall("/grpc.testing.TestService/EmptyCall", new byte[] { }, out result, GPRTimespec.GPRInfFuture);

				Console.WriteLine("result length is " + result.Length + " bytes");
			}
			GRPCUtils.Shutdown();
		}


		public static void Main (string[] args)
		{
			GRPCUtils.Init();
			using (Channel channel = new Channel("127.0.0.1:23456"))
			{
				IMathServiceClient stub = new MathServiceClientImpl (channel);
				Examples.DivExample(stub);

				Examples.SumExample(stub);

				Examples.DivManyExample(stub);
			}
			GRPCUtils.Shutdown();
		}
	}
}
