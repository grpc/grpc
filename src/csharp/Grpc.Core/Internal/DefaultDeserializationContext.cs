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
using System;
using System.Threading;

namespace Grpc.Core.Internal
{
    internal class DefaultDeserializationContext : DeserializationContext
    {
        static readonly ThreadLocal<DefaultDeserializationContext> threadLocalInstance =
            new ThreadLocal<DefaultDeserializationContext>(() => new DefaultDeserializationContext(), false);

        byte[] payload;
        bool alreadyCalledPayloadAsNewBuffer;

        public DefaultDeserializationContext()
        {
            Reset();
        }

        public override int PayloadLength => payload.Length;

        public override byte[] PayloadAsNewBuffer()
        {
            GrpcPreconditions.CheckState(!alreadyCalledPayloadAsNewBuffer);
            alreadyCalledPayloadAsNewBuffer = true;
            return payload;
        }

        public void Initialize(byte[] payload)
        {
            this.payload = GrpcPreconditions.CheckNotNull(payload);
            this.alreadyCalledPayloadAsNewBuffer = false;
        }

        public void Reset()
        {
            this.payload = null;
            this.alreadyCalledPayloadAsNewBuffer = true;  // mark payload as read
        }

        public static DefaultDeserializationContext GetInitializedThreadLocal(byte[] payload)
        {
            var instance = threadLocalInstance.Value;
            instance.Initialize(payload);
            return instance;
        }
    }
}
