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
    // this entire thing can be replaced with ValueTask<T> trivially, when available
    internal struct TaskLite<T> : INotifyCompletion, ICriticalNotifyCompletion
    {
        // dummy no-op method so that the API doesn't change if we can switch to ValueTask<T>,
        // and we haven't changed the await behavior
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public TaskLite<T> ConfigureAwait(bool _)
        {
            return this;
        }

        /// <summary>
        /// Consumes the incomplete operation as a Task; this should only be used once, and
        /// should be used *in place of* await, not in addition to
        /// </summary>
        public Task<T> AsTask()
        {
            if (!IsCompleted) return Awaited();

            T result;
            try
            {
                result = GetResult();
            }
            catch (Exception ex)
            {
                return TaskSource<T>.FromException(ex);
            }
            return Task.FromResult(result);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private async Task<T> Awaited()
        {
            return await this;
        }

        private readonly short token;
        private readonly TaskSource<T>.State task;

        public override string ToString()
        {
            return typeof(TaskLite<T>).FullName;
        }
        public override int GetHashCode()
        {
            throw new NotSupportedException();
        }
        public override bool Equals(object obj)
        {
            throw new NotSupportedException();
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        internal TaskLite(TaskSource<T>.State task, short token)
        {
            GrpcPreconditions.CheckNotNull(task);
            this.token = token;
            this.task = task;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public void OnCompleted(Action continuation)
        {
            task.Schedule(token, continuation);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public void UnsafeOnCompleted(Action continuation)
        {
            task.Schedule(token, continuation);
        }

        public bool IsCompleted
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get { return task.IsCompleted(token); }
        }

        public bool IsFaulted
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get { return task.IsFaulted(token); }
        }

        public bool IsCompletedSuccessfully
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get { return IsCompleted & !IsFaulted; }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public TaskLite<T> GetAwaiter() { return this; }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public T GetResult()
        {
            return task.GetResult(token);
        }
    }

    internal struct TaskSource<T>
    {
        public bool HasTask
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get { return state != null; }
        }

        public TaskLite<T> Task
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get { return new TaskLite<T>(state, token); }
        }

        private readonly short token;
        private readonly State state;

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private TaskSource(State state, short token)
        {
            GrpcPreconditions.CheckNotNull(state);
            this.token = token;
            this.state = state;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public void SetResult(T value) { state.Complete(token, value, null); }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public void SetException(Exception exception) { state.Complete(token, default(T), exception); }

        private static Task<T> CanceledTaskInstance;
        public static Task<T> CanceledTask
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get { return CanceledTaskInstance ?? (CanceledTaskInstance = CreateCanceled()); }
        }

        public static Task<T> FromException(Exception fault)
        {
            GrpcPreconditions.CheckNotNull(fault);
#if NET45
            var tcs = new TaskCompletionSource<T>();
            tcs.SetException(fault);
            return tcs.Task;
#else
            return System.Threading.Tasks.Task.FromException<T>(fault);
#endif
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private static Task<T> CreateCanceled()
        {
            TaskCompletionSource<T> source = new TaskCompletionSource<T>();
            source.SetCanceled();
            return source.Task;
        }

        private static readonly State[] RecyclePool = new State[16];

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static TaskSource<T> Get()
        {
            var pool = RecyclePool;
            State state = null;
            for (int i = 0; i < pool.Length; i++)
            {
                state = Interlocked.Exchange(ref pool[i], null);
                if (state != null) break;
            }
            if (state == null) state = new State();
            return new TaskSource<T>(state, state.CurrentToken);
        }

        internal sealed class State
        {
            private int currentToken = short.MinValue;
            internal short CurrentToken
            {
                [MethodImpl(MethodImplOptions.AggressiveInlining)]
                get { return (short)Volatile.Read(ref currentToken); }
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            private void NewToken()
            {
                while (true)
                {
                    int newToken = Interlocked.Increment(ref currentToken);
                    if (newToken <= short.MaxValue) return;

                    // otherwise, we overflowed; try to reset
                    if (Interlocked.CompareExchange(ref currentToken, short.MinValue, newToken) == newToken)
                        return; // we successfully reset it to the min

                    // otherwise, we got into a race; redo from start
                }
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            private void CheckToken(short token)
            {
                if (Volatile.Read(ref currentToken) != token) ThrowInvalidToken();
            }
            [MethodImpl(MethodImplOptions.NoInlining)]
            private static void ThrowInvalidToken()
            {
                GrpcPreconditions.CheckState(false, "token mismatch; the task is being accessed incorrectly");
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

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public bool IsCompleted(short token)
            {
                CheckToken(token);
                return (object)Volatile.Read(ref this.scheduled) == CompletedSentinel;
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public bool IsFaulted(short token)
            {

                CheckToken(token);
                var exception = (object)Volatile.Read(ref this.fault);
                return exception != null & (object)exception != NoFault;
            }

            private T resultValue;
            private Exception fault;
            private Action scheduled;

            private static readonly Action CompletedSentinel = () => { };
            private static readonly Exception NoFault = new Exception();

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public void Reset()
            {
                NewToken();
                resultValue = default(T);
                Volatile.Write(ref fault, null);
                Volatile.Write(ref scheduled, null);
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            internal void Schedule(short token, Action continuation)
            {
                CheckToken(token);
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
            internal void Complete(short token, T value, Exception exception)
            {
                CheckToken(token);
                // mark it as complete, and execute anything that was scheduled
                var oldFault = Volatile.Read(ref fault);
                GrpcPreconditions.CheckState(oldFault == null, "SetResult called multiple times");
                resultValue = value;
                oldFault = Interlocked.CompareExchange(ref fault, exception ?? NoFault, null); // races are fun
                                                                                               // note that if this fails, we will have stomped value; but this is an example of incorrect
                                                                                               // API usage - the caller **should not call** SetResult twice per session; fix the caller!
                GrpcPreconditions.CheckState(oldFault == null, "SetResult called multiple times");

                var continuation = Interlocked.Exchange(ref scheduled, CompletedSentinel);
                if (continuation != null) continuation();
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            internal T GetResult(short token)
            {
                CheckToken(token);
                var continuation = Volatile.Read(ref this.scheduled);
                GrpcPreconditions.CheckState((object)continuation == CompletedSentinel, "task is incomplete");
                var fault = Volatile.Read(ref this.fault);
                T result = this.resultValue;

                // we've got everything we need out of here, and GetResult() should
                // only be called once; we can now recycle the object; anyone else who
                // gets it will have a different token
                Recycle();

                if (fault != null & (object)fault != NoFault) throw fault; // yes, the stack-trace will be wrong
                return result;
            }
        }
    }
}
