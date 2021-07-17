#region Copyright notice and license

// Copyright 2017 gRPC authors.
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
using System.Threading;
using System.Collections.Generic;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Pool of objects that combines a shared pool and a thread local pool.
    /// </summary>
    internal class DefaultObjectPool<T> : IObjectPool<T>
        where T : class, IPooledObject<T>
    {
        readonly object myLock = new object();
        readonly Action<T> returnAction;
        readonly Func<T> itemFactory;

        // Queue shared between threads, access needs to be synchronized.
        readonly Queue<T> sharedQueue;
        readonly int sharedCapacity;

        readonly ThreadLocal<ThreadLocalData> threadLocalData;
        readonly int threadLocalCapacity;
        readonly int rentLimit;

        bool disposed;

        /// <summary>
        /// Initializes a new instance of <c>DefaultObjectPool</c> with given shared capacity and thread local capacity.
        /// Thread local capacity should be significantly smaller than the shared capacity as we don't guarantee immediately
        /// disposing the objects in the thread local pool after this pool is disposed (they will eventually be garbage collected
        /// after the thread that owns them has finished).
        /// On average, the shared pool will only be accessed approx. once for every <c>threadLocalCapacity / 2</c> rent or lease
        /// operations.
        /// </summary>
        public DefaultObjectPool(Func<T> itemFactory, int sharedCapacity, int threadLocalCapacity)
        {
            GrpcPreconditions.CheckArgument(sharedCapacity >= 0);
            GrpcPreconditions.CheckArgument(threadLocalCapacity >= 0);
            this.returnAction = Return;
            this.itemFactory = GrpcPreconditions.CheckNotNull(itemFactory, nameof(itemFactory));
            this.sharedQueue = new Queue<T>(sharedCapacity);
            this.sharedCapacity = sharedCapacity;
            this.threadLocalData = new ThreadLocal<ThreadLocalData>(() => new ThreadLocalData(threadLocalCapacity), false);
            this.threadLocalCapacity = threadLocalCapacity;
            this.rentLimit = threadLocalCapacity != 1 ? threadLocalCapacity / 2 : 1;
        }

        /// <summary>
        /// Leases an item from the pool or creates a new instance if the pool is empty.
        /// Attempts to retrieve the item from the thread local pool first.
        /// If the thread local pool is empty, the item is taken from the shared pool
        /// along with more items that are moved to the thread local pool to avoid
        /// prevent acquiring the lock for shared pool too often.
        /// The methods should not be called after the pool is disposed, but it won't
        /// results in an error to do so (after depleting the items potentially left
        /// in the thread local pool, it will continue returning new objects created by the factory).
        /// </summary>
        public T Lease()
        {
            var item = LeaseInternal();
            item.SetReturnToPoolAction(returnAction);
            return item;
        }

        private T LeaseInternal()
        {
            var localData = threadLocalData.Value;
            if (localData.Queue.Count > 0)
            {
                return localData.Queue.Dequeue();
            }
            if (localData.CreateBudget > 0)
            {
                localData.CreateBudget --;
                return itemFactory();
            }

            int itemsMoved = 0;
            T leasedItem = null;
            lock(myLock)
            {
                if (sharedQueue.Count > 0)
                {
                    leasedItem = sharedQueue.Dequeue();
                }
                while (sharedQueue.Count > 0 && itemsMoved < rentLimit)
                {
                    localData.Queue.Enqueue(sharedQueue.Dequeue());
                    itemsMoved ++;
                }
            }

            // If the shared pool didn't contain all rentLimit items,
            // next time we try to lease we will just create those
            // instead of trying to grab them from the shared queue.
            // This is to guarantee we won't be accessing the shared queue too often.
            localData.CreateBudget = rentLimit - itemsMoved;

            return leasedItem ?? itemFactory();
        }

        /// <summary>
        /// Returns an item to the pool.
        /// Attempts to add the item to the thread local pool first.
        /// If the thread local pool is full, item is added to a shared pool,
        /// along with half of the items for the thread local pool, which
        /// should prevent acquiring the lock for shared pool too often.
        /// If called after the pool is disposed, we make best effort not to
        /// add anything to the thread local pool and we guarantee not to add
        /// anything to the shared pool (items will be disposed instead).
        /// </summary>
        public void Return(T item)
        {
            GrpcPreconditions.CheckNotNull(item);

            var localData = threadLocalData.Value;
            if (localData.Queue.Count < threadLocalCapacity && !disposed)
            {
                localData.Queue.Enqueue(item);
                return;
            }
            if (localData.DisposeBudget > 0)
            {
                localData.DisposeBudget --;
                item.Dispose();
                return;
            }

            int itemsReturned = 0;
            int returnLimit = rentLimit + 1;
            lock (myLock)
            {
                if (sharedQueue.Count < sharedCapacity && !disposed)
                {
                    sharedQueue.Enqueue(item);
                    itemsReturned ++;
                }
                while (sharedQueue.Count < sharedCapacity && itemsReturned < returnLimit && !disposed)
                {
                    sharedQueue.Enqueue(localData.Queue.Dequeue());
                    itemsReturned ++;
                }
            }

            // If the shared pool could not accommodate all returnLimit items,
            // next time we try to return we will just dispose the item
            // instead of trying to return them to the shared queue.
            // This is to guarantee we won't be accessing the shared queue too often.
            localData.DisposeBudget = returnLimit - itemsReturned;

            if (itemsReturned == 0)
            {
                localData.DisposeBudget --;
                item.Dispose();
            }
        }

        public void Dispose()
        {
            lock (myLock)
            {
                if (!disposed)
                {
                    disposed = true;

                    while (sharedQueue.Count > 0)
                    {
                        sharedQueue.Dequeue().Dispose();
                    }
                }
            }
        }

        class ThreadLocalData
        {
            public ThreadLocalData(int capacity)
            {
                this.Queue = new Queue<T>(capacity);
            }

            public Queue<T> Queue { get; }
            public int CreateBudget { get; set; }
            public int DisposeBudget { get; set; }
        }
    }
}
