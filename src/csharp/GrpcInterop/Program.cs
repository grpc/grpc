using System;
using System.Runtime.InteropServices;
using Google.GRPC.Interop;

namespace Google.GRPC.Demo
{
	class MainClass
	{
		public static void Main (string[] args)
		{
			GRPCUtils.Init();

			using (Channel channel = new Channel("127.0.0.1:12345"))
			{
			    byte[] result;
				Status status = channel.StartBlockingRpc ("/grpc.testing.TestService/EmptyCall", new byte[] { }, out result, GPRTimespec.GPRInfFuture);

				Console.WriteLine("result lenght is " + result.Length + " bytes");
			}
			GRPCUtils.Shutdown();
		}
	}
}
