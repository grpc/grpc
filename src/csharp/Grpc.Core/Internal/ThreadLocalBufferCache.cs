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
using System.Threading;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Avoids repeated allocations of byte arrays in situations where the usage of those buffers is limited to the current scope.
    /// </summary>
    internal class ThreadLocalBufferCache
    {
        static readonly byte[] EmptyByteArray = new byte[0];
        readonly ThreadLocal<WeakReference<byte[]>> bufferCache;

        public ThreadLocalBufferCache()
        {
            this.bufferCache = new ThreadLocal<WeakReference<byte[]>>();
        }

        /// <summary>
        /// Rents a thread local buffer of size at least minSize (the buffer returned can be bigger).
        /// The buffer is cached as a weak reference, so GC is free to collect it if memory pressure increases.
        /// Because there's only 1 buffer per thread available, you must only use it from within the same thread
        /// and stop using any references to it before you yield control of the current thread.
        /// As a rule of thumb, you should stop using the rented buffer before leaving the current scope or before
        /// performing any async operations.
        /// </summary>
        public byte[] RentForCurrentScope(int minSize)
        {
            GrpcPreconditions.CheckArgument(minSize >=0);
            if (minSize == 0)
            {
                return EmptyByteArray;
            }

            byte[] buffer = null;
            if (bufferCache.IsValueCreated)
            {
                bufferCache.Value.TryGetTarget(out buffer);
            }
            // the cached buffer size only grows here, but that's fine because GC can collect the weak reference 
            // if memory pressure increases.
            if (buffer == null || buffer.Length <= minSize)
            {
                // TODO(jtattermusch): apply some reasonable ceiling for buffer size.
                // TODO(jtattermusch): we could use a slightly bigger size than required to prevent
                // excessive reallocations if requested size grows slowly.
                buffer = new byte[minSize];
                bufferCache.Value = new WeakReference<byte[]>(buffer);
            }
            return buffer;
        }
    }
}
