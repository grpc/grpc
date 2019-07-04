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

        [ThreadStatic]
        private static ReusableTaskLite spare1, spare2, spare3;

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static ReusableTaskLite TryGet(ref ReusableTaskLite storage)
        {
            return Interlocked.Exchange(ref storage, null);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static ReusableTaskLite Get()
        {
            return TryGet(ref spare1) ?? TryGet(ref spare2) ?? TryGet(ref spare3) ?? new ReusableTaskLite();
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static bool TryPut(ref ReusableTaskLite storage, ReusableTaskLite value)
        {
            return Interlocked.CompareExchange(ref storage, value, null) == null;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public bool Recycle()
        {
            Reset();
            return TryPut(ref spare1, this) || TryPut(ref spare2, this) || TryPut(ref spare3, this);
        }



        public struct Awaitable : INotifyCompletion, ICriticalNotifyCompletion
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public bool Recycle()
            {
                return task.Recycle();
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
                if (fault != null) throw fault; // yes, the stack-trace will be wrong
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
