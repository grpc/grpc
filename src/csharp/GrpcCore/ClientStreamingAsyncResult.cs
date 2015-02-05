using System;
using System.Threading.Tasks;

namespace Google.GRPC.Core
{
    /// <summary>
    /// Return type for client streaming async method.
    /// </summary>
    public struct ClientStreamingAsyncResult<TRequest, TResponse>
    {
        readonly Task<TResponse> task;
        readonly IObserver<TRequest> inputs;

        public ClientStreamingAsyncResult(Task<TResponse> task, IObserver<TRequest> inputs)
        {
            this.task = task;
            this.inputs = inputs;
        }

        public Task<TResponse> Task
        {
            get
            {
                return this.task;
            }
        }

        public IObserver<TRequest> Inputs
        {
            get
            {
                return this.inputs;
            }
        }
    }
}

