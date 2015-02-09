using System;
using System.Threading.Tasks;
using System.Collections.Generic;

namespace Google.GRPC.Core.Utils
{
    public class RecordingObserver<T> : IObserver<T>
    {
        TaskCompletionSource<List<T>> tcs = new TaskCompletionSource<List<T>>();
        List<T> data = new List<T>();

        public void OnCompleted()
        {
            tcs.SetResult(data);
        }

        public void OnError(Exception error)
        {
            tcs.SetException(error);
        }

        public void OnNext(T value)
        {
            data.Add(value);
        }

        public Task<List<T>> ToList() {
            return tcs.Task;
        }
    }
}

