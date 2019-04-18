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
    public class MetadataArraySafeHandleTest
    {
        [Test]
        public void CreateEmptyAndDestroy()
        {
            var nativeMetadata = MetadataArraySafeHandle.Create(new Metadata());
            nativeMetadata.Dispose();
        }

        [Test]
        public void CreateAndDestroy()
        {
            var metadata = new Metadata
            {
                { "host", "somehost" },
                { "header2", "header value" },
            };
            var nativeMetadata = MetadataArraySafeHandle.Create(metadata);
            nativeMetadata.Dispose();
        }

        [Test]
        public void ReadMetadataFromPtrUnsafe()
        {
            var metadata = new Metadata
            {
                { "host", "somehost" },
                { "header2", "header value" }
            };
            var nativeMetadata = MetadataArraySafeHandle.Create(metadata);

            var copy = MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(nativeMetadata.Handle);
            Assert.AreEqual(2, copy.Count);

            Assert.AreEqual("host", copy[0].Key);
            Assert.AreEqual("somehost", copy[0].Value);
            Assert.AreEqual("header2", copy[1].Key);
            Assert.AreEqual("header value", copy[1].Value);

            nativeMetadata.Dispose();
        }
    }
}
