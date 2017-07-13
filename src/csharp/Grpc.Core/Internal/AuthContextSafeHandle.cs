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
