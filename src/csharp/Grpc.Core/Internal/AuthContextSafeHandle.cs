#region Copyright notice and license

// Copyright 2017, Google Inc.
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
using System.Runtime.InteropServices;
using System.Text;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_auth_context
    /// </summary>
    internal class AuthContextSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        private AuthContextSafeHandle()
        {
        }

        /// <summary>
        /// Copies contents of the native auth context into a new <c>AuthContext</c> instance.
        /// </summary>
        public AuthContext ToAuthContext()
        {
            if (IsInvalid)
            {
                return new AuthContext(null, new Dictionary<string, List<AuthProperty>>());
            }

            var peerIdentityPropertyName = Marshal.PtrToStringAnsi(Native.grpcsharp_auth_context_peer_identity_property_name(this));

            var propertiesDict = new Dictionary<string, List<AuthProperty>>();

            var it = Native.grpcsharp_auth_context_property_iterator(this);
            IntPtr authPropertyPtr = IntPtr.Zero;
            while ((authPropertyPtr = Native.grpcsharp_auth_property_iterator_next(ref it)) != IntPtr.Zero)
            {
                var authProperty = PtrToAuthProperty(authPropertyPtr);

                if (!propertiesDict.ContainsKey(authProperty.Name))
                {
                    propertiesDict[authProperty.Name] = new List<AuthProperty>();
                }
                propertiesDict[authProperty.Name].Add(authProperty);
            }

            return new AuthContext(peerIdentityPropertyName, propertiesDict);
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_auth_context_release(handle);
            return true;
        }

        private AuthProperty PtrToAuthProperty(IntPtr authPropertyPtr)
        {
            var nativeAuthProperty = (NativeAuthProperty) Marshal.PtrToStructure(authPropertyPtr, typeof(NativeAuthProperty));
            var name = Marshal.PtrToStringAnsi(nativeAuthProperty.Name);
            var valueBytes = new byte[(int) nativeAuthProperty.ValueLength];
            Marshal.Copy(nativeAuthProperty.Value, valueBytes, 0, (int)nativeAuthProperty.ValueLength);
            return AuthProperty.CreateUnsafe(name, valueBytes);
        }

        /// <summary>
        /// grpc_auth_property
        /// </summary>
        internal struct NativeAuthProperty
        {
            public IntPtr Name;
            public IntPtr Value;
            public UIntPtr ValueLength;
        }

        /// <summary>
        /// grpc_auth_property_iterator
        /// </summary>
        internal struct NativeAuthPropertyIterator
        {
            public IntPtr AuthContext;
            public UIntPtr Index;
            public IntPtr Name;
        }
    }
}
