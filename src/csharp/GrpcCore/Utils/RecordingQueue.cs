using System;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Collections.Concurrent;

namespace Google.GRPC.Core.Utils
{
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

