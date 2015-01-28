using System;
using System.Runtime.InteropServices;
using Google.GRPC.Wrappers;
using System.Threading;
using math;

namespace Google.GRPC.Demo
{
	class MainClass
    {
		public static void Main (string[] args)
		{
			using (Channel channel = new Channel("127.0.0.1:23456"))
			{
				IMathServiceClient stub = new MathServiceClientImpl (channel);
				Examples.DivExample(stub);

				Examples.SumExample(stub);

				Examples.DivManyExample(stub);
			}
           
            GrpcEnvironment.Shutdown();
		}
	}
}
