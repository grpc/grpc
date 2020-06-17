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
using Grpc.Core.Internal;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class ChannelCredentialsTest
    {
        [Test]
        public void InsecureCredentials_IsNonComposable()
        {
            Assert.IsFalse(ChannelCredentials.Insecure.IsComposable);
        }

        [Test]
        public void ChannelCredentials_CreateComposite()
        {
            var composite = ChannelCredentials.Create(new FakeChannelCredentials(true), new FakeCallCredentials());
            Assert.IsFalse(composite.IsComposable);

            Assert.Throws(typeof(ArgumentNullException), () => ChannelCredentials.Create(null, new FakeCallCredentials()));
            Assert.Throws(typeof(ArgumentNullException), () => ChannelCredentials.Create(new FakeChannelCredentials(true), null));

            // forbid composing non-composable
            var ex = Assert.Throws(typeof(ArgumentException), () => ChannelCredentials.Create(new FakeChannelCredentials(false), new FakeCallCredentials()));
            Assert.AreEqual("CallCredentials can't be composed with FakeChannelCredentials. CallCredentials must be used with secure channel credentials like SslCredentials.", ex.Message);
        }

        [Test]
        public void ChannelCredentials_NativeCredentialsAreReused()
        {
            // always returning the same native object is critical for subchannel sharing to work with secure channels
            var creds = new SslCredentials();
            var nativeCreds1 = creds.ToNativeCredentials();
            var nativeCreds2 = creds.ToNativeCredentials();
            Assert.AreSame(nativeCreds1, nativeCreds2);
        }
    }
}
