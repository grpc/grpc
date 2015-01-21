using System;
using System.Diagnostics;

namespace Google.GRPC.Interop
{
	public static class Utils
	{
		public static void AssertCallOk(GRPCCallError callError)
		{
			Trace.Assert(callError == GRPCCallError.GRPC_CALL_OK, "Status not GRPC_CALL_OK");
		}
	}
}

