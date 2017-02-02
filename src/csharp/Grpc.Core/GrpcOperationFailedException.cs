using System;
namespace Grpc.Core
{
    public class GrpcOperationFailedException : InvalidOperationException
    {
        public GrpcOperationFailedException()
        {
        }
        
        public GrpcOperationFailedException(string message) : base(message)
        {
        }
        
        public GrpcOperationFailedException(string message, Exception inner) : base(message, inner)
        {
        }
    }
}

