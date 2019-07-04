#region Copyright notice and license

// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#endregion

using System;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal class ReusableTaskLite
    {
        public static readonly Task CanceledTask;
        static ReusableTaskLite()
        {
            TaskCompletionSource<object> source = new TaskCompletionSource<object>();
            source.SetCanceled();
            CanceledTask = source.Task;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private ReusableTaskLite() { }

        private static readonly ReusableTaskLite[] RecyclePool = new ReusableTaskLite[16];

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static ReusableTaskLite Get()
        {
            var pool = RecyclePool;
            for (int i = 0; i < pool.Length; i++)
            {
                var obj = Interlocked.Exchange(ref pool[i], null);
                if (obj != null)
                {
                    return obj;
                }
            }
            return new ReusableTaskLite();
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public void Recycle()
        {
            Reset();
            var pool = RecyclePool;
            for (int i = 0; i < pool.Length; i++)
            {
                if (Interlocked.CompareExchange(ref pool[i], this, null) == null)
                {
                    return;
                }
            }
        }

        public struct Awaitable : INotifyCompletion, ICriticalNotifyCompletion
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public void Recycle()
            {
                task.Recycle();
            }

            public override string ToString()
            {
                return typeof(Awaitable).FullName;
            }
            public override int GetHashCode()
            {
                throw new NotSupportedException();
            }
            public override bool Equals(object obj)
            {
                throw new NotSupportedException();
            }

            private readonly ReusableTaskLite task;
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public Awaitable(ReusableTaskLite task)
            {
                GrpcPreconditions.CheckNotNull(task);
                this.task = task;
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public void OnCompleted(Action continuation)
            {
                task.Schedule(continuation);
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public void UnsafeOnCompleted(Action continuation)
            {
                task.Schedule(continuation);
            }

            public bool IsCompleted
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get { return (object)Volatile.Read(ref task.scheduled) == CompletedSentinel; }
            }

            public bool IsFaulted
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get
                {
                    var exception = (object)Volatile.Read(ref task.fault);
                    return exception != null & (object)exception != NoFault;
                }
            }

            public bool IsCompletedSuccessfully
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get { return IsCompleted & !IsFaulted; }
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public ReusableTaskLite GetResult()
            {
                var continuation = Volatile.Read(ref task.scheduled);
                GrpcPreconditions.CheckState((object)continuation == CompletedSentinel, "task is incomplete");
                var fault = Volatile.Read(ref task.fault);
                if (fault != null & (object)fault != NoFault) throw fault; // yes, the stack-trace will be wrong
                return task;
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public Awaitable GetAwaiter() { return this; }
        }

        public Awaitable Task
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get { return new Awaitable(this); }
        }

        private Exception fault;
        private Action scheduled;

        private static readonly Action CompletedSentinel = () => { };
        private static readonly Exception NoFault = new Exception();

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public void Reset()
        {
            Volatile.Write(ref fault, null);
            Volatile.Write(ref scheduled, null);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private void Schedule(Action continuation)
        {
            GrpcPreconditions.CheckNotNull(continuation);
            var oldValue = Interlocked.CompareExchange(ref scheduled, continuation, null);
            if (ReferenceEquals(oldValue, null))
            {
                // fine, we added our continuation
            }
            else if (ReferenceEquals(oldValue, CompletedSentinel))
            {
                // the task was already complete; call it inline
                continuation();
            }
            else
            {
                // multiple continuations: not supported
                GrpcPreconditions.CheckState(false, "Only one continuation can be pending at a time");
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public void SetResult() { Complete(null); }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public void SetException(Exception exception) { Complete(exception); }


        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private void Complete(Exception exception = null)
        {
            // mark it as complete, and execute anything that was scheduled
            var oldFault = Interlocked.CompareExchange(ref fault, exception ?? NoFault, null);
            GrpcPreconditions.CheckState(oldFault == null, "SetResult called multiple times");

            var continuation = Interlocked.Exchange(ref scheduled, CompletedSentinel);
            if (continuation != null) continuation();
        }
    }
}
