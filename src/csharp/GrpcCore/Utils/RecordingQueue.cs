using System;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Collections.Concurrent;

namespace Google.GRPC.Core.Utils
{
    /// <summary>
    /// Observer that allows us to await incoming messages one-by-one.
    /// The implementation is not ideal and class will be probably replaced 
    /// by something more versatile in the future.
    /// </summary>
    public class RecordingQueue<T> : IObserver<T>
    {
        readonly BlockingCollection<T> queue = new BlockingCollection<T>();
        TaskCompletionSource<object> tcs = new TaskCompletionSource<object>();

        public void OnCompleted()
        {
            tcs.SetResult(null);
        }

        public void OnError(Exception error)
        {
            tcs.SetException(error);
        }

        public void OnNext(T value)
        {
            queue.Add(value);
        }

        public BlockingCollection<T> Queue
        {
            get
            {
                return queue;
            }
        }

        public Task Finished
        {
            get
            {
                return tcs.Task;
            }
        }
    }
}

