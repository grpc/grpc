using System;
using System.Runtime.InteropServices;
using Google.GRPC.Core;
using System.Threading;

namespace math
{
	class MathClient
    {
		public static void Main (string[] args)
		{
            GrpcEnvironment.Initialize();

			using (Channel channel = new Channel("127.0.0.1:23456"))
			{
				MathGrpc.IMathServiceClient stub = new MathGrpc.MathServiceClientStub(channel);
				MathExamples.DivExample(stub);

                MathExamples.FibExample(stub);

				MathExamples.SumExample(stub);

				MathExamples.DivManyExample(stub);
			}
           
            GrpcEnvironment.Shutdown();
		}
	}
}
