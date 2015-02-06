using System;

namespace Google.GRPC.Core
{
    public class RpcException : Exception
    {
        private readonly Status status;

        public RpcException(Status status)
        {
            this.status = status;
        }

        public RpcException(Status status, string message) : base(message)
        {
            this.status = status;
        }

        public Status Status {
            get
            {
                return status;
            }
        }
    }
}

