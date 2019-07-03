#region Copyright notice and license

// Copyright 2018 The gRPC Authors
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

using Grpc.Core.Utils;
using System.Threading;

namespace Grpc.Core.Internal
{
    internal class DefaultSerializationContext : SerializationContext
    {
        static readonly ThreadLocal<DefaultSerializationContext> threadLocalInstance =
            new ThreadLocal<DefaultSerializationContext>(() => new DefaultSerializationContext(), false);

        bool isComplete;
        byte[] payload;

        public DefaultSerializationContext()
        {
            Reset();
        }

        public override void Complete(byte[] payload)
        {
            GrpcPreconditions.CheckState(!isComplete);
            this.isComplete = true;
            this.payload = payload;
        }

        internal byte[] GetPayload()
        {
            return this.payload;
        }

        public void Reset()
        {
            this.isComplete = false;
            this.payload = null;
        }

        public static DefaultSerializationContext GetInitializedThreadLocal()
        {
            var instance = threadLocalInstance.Value;
            instance.Reset();
            return instance;
        }
    }
}
