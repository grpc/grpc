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
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class CompletionQueueEventTest
    {
        [Test]
        public void CompletionQueueEventSizeIsNativeSize()
        {
            #pragma warning disable 0618
            // We need to use the obsolete non-generic version of Marshal.SizeOf because the generic version is not available in net45
            Assert.AreEqual(CompletionQueueEvent.NativeSize, Marshal.SizeOf(typeof(CompletionQueueEvent)));
            #pragma warning restore 0618
        }
    }
}
