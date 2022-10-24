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
        public void ChannelCredentials_CreateComposite()
        {
            var composite = ChannelCredentials.Create(new FakeChannelCredentials(true), new FakeCallCredentials());
            Assert.IsFalse(composite.IsComposable);

            Assert.Throws(typeof(ArgumentNullException), () => ChannelCredentials.Create(null, new FakeCallCredentials()));
            Assert.Throws(typeof(ArgumentNullException), () => ChannelCredentials.Create(new FakeChannelCredentials(true), null));
        }

        [Test]
        public void ChannelCredentials_NativeCredentialsAreReused()
        {
            // always returning the same native object is critical for subchannel sharing to work with secure channels
            var creds = new SslCredentials();
            var nativeCreds1 = creds.ToNativeCredentials();
            var nativeCreds2 = creds.ToNativeCredentials();
            Assert.AreSame(nativeCreds1, nativeCreds2);

            var nativeCreds3 = ChannelCredentials.SecureSsl.ToNativeCredentials();
            var nativeCreds4 = ChannelCredentials.SecureSsl.ToNativeCredentials();
            Assert.AreSame(nativeCreds3, nativeCreds4);
        }

        [Test]
        public void ChannelCredentials_MalformedSslCredentialsCanStillCreateNativeCredentials()
        {
            // pass malformed root pem certs, but creation of native credentials still passes,
            // since the credentials are parsed lazily by the C core.
            using (var nativeCreds = new SslCredentials("MALFORMED_ROOT_CERTS_THAT_WILL_THROW_WHEN_CREATING_NATIVE_CREDENTIALS").ToNativeCredentials())
            {
                Assert.IsFalse(nativeCreds.IsInvalid);
            }
        }
    }
}
