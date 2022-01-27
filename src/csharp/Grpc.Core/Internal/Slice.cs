#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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
using System.Runtime.InteropServices;
using System.Threading;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Slice of native memory.
    /// Rough equivalent of grpc_slice (but doesn't support inlined slices, just a pointer to data and length)
    /// </summary>
    internal struct Slice
    {
        private readonly IntPtr dataPtr;
        private readonly int length;
     
        public Slice(IntPtr dataPtr, int length)
        {
            this.dataPtr = dataPtr;
            this.length = length;
        }

        public int Length => length;

        public Span<byte> ToSpanUnsafe()
        {
            unsafe
            {
                return new Span<byte>((byte*) dataPtr, length);
            }
        }

        /// <summary>
        /// Returns a <see cref="System.String"/> that represents the current <see cref="Grpc.Core.Internal.Slice"/>.
        /// </summary>
        public override string ToString()
        {
            return string.Format("[Slice: dataPtr={0}, length={1}]", dataPtr, length);
        }
    }
}
