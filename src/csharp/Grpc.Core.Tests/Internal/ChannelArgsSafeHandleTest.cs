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
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class ChannelArgsSafeHandleTest
    {
        [Test]
        public void CreateEmptyAndDestroy()
        {
            var channelArgs = ChannelArgsSafeHandle.Create(0);
            channelArgs.Dispose();
        }

        [Test]
        public void CreateNonEmptyAndDestroy()
        {
            var channelArgs = ChannelArgsSafeHandle.Create(5);
            channelArgs.Dispose();
        }

        [Test]
        public void CreateNullAndDestroy()
        {
            var channelArgs = ChannelArgsSafeHandle.CreateNull();
            channelArgs.Dispose();
        }

        [Test]
        public void CreateFillAndDestroy()
        {
            var channelArgs = ChannelArgsSafeHandle.Create(3);
            channelArgs.SetInteger(0, "somekey", 12345);
            channelArgs.SetString(1, "somekey", "abcdefghijkl");
            channelArgs.SetString(2, "somekey", "XYZ");
            channelArgs.Dispose();
        }
    }
}
