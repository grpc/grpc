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
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_channel_args from <c>grpc/grpc.h</c>
    /// </summary>
    internal class ChannelArgsSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        private ChannelArgsSafeHandle()
        {
        }

        public static ChannelArgsSafeHandle CreateNull()
        {
            return new ChannelArgsSafeHandle();
        }

        public static ChannelArgsSafeHandle Create(int size)
        {
            return Native.grpcsharp_channel_args_create(new UIntPtr((uint)size));
        }

        public void SetString(int index, string key, string value)
        {
            Native.grpcsharp_channel_args_set_string(this, new UIntPtr((uint)index), key, value);
        }

        public void SetInteger(int index, string key, int value)
        {
            Native.grpcsharp_channel_args_set_integer(this, new UIntPtr((uint)index), key, value);
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_channel_args_destroy(handle);
            return true;
        }
    }
}
