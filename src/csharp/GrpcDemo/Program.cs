using System;
using System.Runtime.InteropServices;
using Google.GRPC.Core;
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

				MathGrpc.IMathServiceClient stub = new MathGrpc.MathServiceClientStub(channel);
				Examples.DivExample(stub);

                Examples.FibExample(stub);

				Examples.SumExample(stub);

				Examples.DivManyExample(stub);
			}
           
            GrpcEnvironment.Shutdown();
		}
	}
}
