using System;
using System.Runtime.InteropServices;
using Google.GRPC.Interop;
using System.Threading;
using math;

namespace Google.GRPC.Demo
{
	class MainClass
	{
		private static void DoEmptyCall() {
			Utils.Init();

			using (Channel channel = new Channel("127.0.0.1:12345"))
			{
				byte[] result;
				Status status = channel.SimpleBlockingCall("/grpc.testing.TestService/EmptyCall", new byte[] { }, out result, Timespec.InfFuture, default(CancellationToken));

				Console.WriteLine("result length is " + result.Length + " bytes");
			}
			Utils.Shutdown();
		}




		public static void Main (string[] args)
		{
            //TestUsing();

			Utils.Init();
			using (Channel channel = new Channel("127.0.0.1:23456"))
			{
				IMathServiceClient stub = new MathServiceClientImpl (channel);
				Examples.DivExample(stub);

				Examples.SumExample(stub);

				Examples.DivManyExample(stub);
			}
			Utils.Shutdown();
		}

//        public class TestClass : WN {
//
//            public TestClass() : base(() => new IntPtr(5))
//            {
//
//            }
//
//            protected override void Destroy()
//            {
//                Console.WriteLine("Destroying: " + this.RawPointer);
//            }
//        }
//
//        public abstract class WN : IDisposable
//        {
//            IntPtr rawPtr = IntPtr.Zero;
//
//            /// <summary>
//            /// Runs the delegate to allocate resource.
//            /// </summary>
//            protected WN(Func<IntPtr> allocDelegate)
//            {
//                this.rawPtr = allocDelegate();
//
//                throw new Exception("exception thrown");
//            }
//
//            ~WN()
//            {
//                Dispose(false);
//            }
//
//            public IntPtr RawPointer
//            {
//                get
//                {
//                    return rawPtr;
//                }
//            }
//
//            /// <summary>
//            /// Destroys the object represented by rawPtr.
//            /// </summary>
//            protected abstract void Destroy();
//
//            public void Dispose()
//            {
//                Dispose(true);
//                GC.SuppressFinalize(this);
//            }
//
//            protected virtual void Dispose(bool disposing)
//            {
//                if (disposing && rawPtr != IntPtr.Zero)
//                {
//                    try
//                    {
//                        Destroy();
//                    }
//                    finally
//                    {
//                        rawPtr = IntPtr.Zero;
//                    }
//                }
//            }
//        }
	}
}
