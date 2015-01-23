using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
	public class Status
	{
		readonly GRPCStatusCode statusCode;
		readonly string detail;

		public Status(GRPCStatusCode statusCode, string detail)
		{
			this.statusCode = statusCode;
			this.detail = detail;
		}

		public GRPCStatusCode StatusCode
		{
			get
			{
				return statusCode;
			}
		}

		public string Detail
		{
			get
			{
				return detail;
			}
		}
	}
}