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
using System.Text;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// A property of an <see cref="AuthContext"/>.
    /// Note: experimental API that can change or be removed without any prior notice.
    /// </summary>
    public class AuthProperty
    {
        static readonly Encoding EncodingUTF8 = System.Text.Encoding.UTF8;
        string name;
        byte[] valueBytes;
        string lazyValue;

        private AuthProperty(string name, byte[] valueBytes)
        {
            this.name = GrpcPreconditions.CheckNotNull(name);
            this.valueBytes = GrpcPreconditions.CheckNotNull(valueBytes);
        }

        /// <summary>
        /// Gets the name of the property.
        /// </summary>
        public string Name
        {
            get
            {
                return name;
            }
        }

        /// <summary>
        /// Gets the string value of the property.
        /// </summary>
        public string Value
        {
            get
            {
                return lazyValue ?? (lazyValue = EncodingUTF8.GetString(this.valueBytes));
            }
        }

        /// <summary>
        /// Gets the binary value of the property.
        /// </summary>
        public byte[] ValueBytes
        {
            get
            {
                var valueCopy = new byte[valueBytes.Length];
                Buffer.BlockCopy(valueBytes, 0, valueCopy, 0, valueBytes.Length);
                return valueCopy;
            }
        }

        /// <summary>
        /// Creates an instance of <c>AuthProperty</c>.
        /// </summary>
        /// <param name="name">the name</param>
        /// <param name="valueBytes">the binary value of the property</param>
        public static AuthProperty Create(string name, byte[] valueBytes)
        {
            GrpcPreconditions.CheckNotNull(valueBytes);
            var valueCopy = new byte[valueBytes.Length];
            Buffer.BlockCopy(valueBytes, 0, valueCopy, 0, valueBytes.Length);
            return new AuthProperty(name, valueCopy);
        }

        /// <summary>
        /// Gets the binary value of the property (without making a defensive copy).
        /// </summary>
        internal byte[] ValueBytesUnsafe
        {
            get
            {
                return valueBytes;
            }
        }

        /// <summary>
        /// Creates and instance of <c>AuthProperty</c> without making a defensive copy of <c>valueBytes</c>.
        /// </summary>
        internal static AuthProperty CreateUnsafe(string name, byte[] valueBytes)
        {
            return new AuthProperty(name, valueBytes);
        }
    }
}
