using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Wrappers
{
	// TODO: this should not be in Wrappers namespace.
	/// <summary>
	/// Represents RPC result.
	/// </summary>
	public struct Status
	{
		readonly StatusCode statusCode;
		readonly string detail;

		public Status(StatusCode statusCode, string detail)
		{
			this.statusCode = statusCode;
			this.detail = detail;
		}

		public StatusCode StatusCode
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