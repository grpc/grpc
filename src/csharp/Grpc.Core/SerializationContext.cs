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

namespace Grpc.Core
{
    /// <summary>
    /// Provides storage for payload when serializing a message.
    /// </summary>
    public abstract class SerializationContext
    {
        /// <summary>
        /// Use the byte array as serialized form of current message and mark serialization process as complete.
        /// Complete() can only be called once. By calling this method the caller gives up the ownership of the
        /// payload which must not be accessed afterwards.
        /// </summary>
        /// <param name="payload">the serialized form of current message</param>
        public abstract void Complete(byte[] payload);
    }
}
