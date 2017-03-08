#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endregion

using System;
using System.Collections.Generic;
using System.Linq;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// A property of an <see cref="AuthContext"/>.
    /// Note: experimental API that can change or be removed without any prior notice.
    /// </summary>
    public class AuthProperty
    {
        string name;
        byte[] valueBytes;
        Lazy<string> value;

        private AuthProperty(string name, byte[] valueBytes)
        {
            this.name = GrpcPreconditions.CheckNotNull(name);
            this.valueBytes = GrpcPreconditions.CheckNotNull(valueBytes);
            this.value = new Lazy<string>(() => MarshalUtils.GetStringUTF8(this.valueBytes));
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
                return value.Value;
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
