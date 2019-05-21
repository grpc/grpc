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
            Assert.Throws(typeof(ArgumentException), () => ChannelCredentials.Create(new FakeChannelCredentials(false), new FakeCallCredentials()));
        }

        [Test]
        public void ChannelCredentials_NativeCredentialsAreReused()
        {
            // always returning the same native object is critical for subchannel sharing to work with secure channels
            var creds = new SslCredentials();
            var nativeCreds1 = creds.GetNativeCredentials();
            var nativeCreds2 = creds.GetNativeCredentials();
            Assert.AreSame(nativeCreds1, nativeCreds2);
        }

        [Test]
        public void ChannelCredentials_CreateExceptionIsCached()
        {
            var creds = new ChannelCredentialsWithCreateNativeThrows();
            var ex1 = Assert.Throws(typeof(Exception), () => creds.GetNativeCredentials());
            var ex2 = Assert.Throws(typeof(Exception), () => creds.GetNativeCredentials());
            Assert.AreSame(ex1, ex2);
        }

        internal class ChannelCredentialsWithCreateNativeThrows : ChannelCredentials
        {
            internal override bool IsComposable => false;

            internal override ChannelCredentialsSafeHandle CreateNativeCredentials()
            {
                throw new Exception("Creation of native credentials has failed on purpose.");
            }
        }
    }
}
