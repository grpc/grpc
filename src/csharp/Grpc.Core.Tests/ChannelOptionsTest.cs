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
using System.Collections.Generic;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class ChannelOptionsTest
    {
        [Test]
        public void IntOption()
        {
            var option = new ChannelOption("somename", 1);

            Assert.AreEqual(ChannelOption.OptionType.Integer, option.Type);
            Assert.AreEqual("somename", option.Name);
            Assert.AreEqual(1, option.IntValue);
            Assert.Throws(typeof(InvalidOperationException), () => { var s = option.StringValue; });
        }

        [Test]
        public void StringOption()
        {
            var option = new ChannelOption("somename", "ABCDEF");

            Assert.AreEqual(ChannelOption.OptionType.String, option.Type);
            Assert.AreEqual("somename", option.Name);
            Assert.AreEqual("ABCDEF", option.StringValue);
            Assert.Throws(typeof(InvalidOperationException), () => { var s = option.IntValue; });
        }

        [Test]
        public void ConstructorPreconditions()
        {
            Assert.Throws(typeof(ArgumentNullException), () => { new ChannelOption(null, "abc"); });
            Assert.Throws(typeof(ArgumentNullException), () => { new ChannelOption(null, 1); });
            Assert.Throws(typeof(ArgumentNullException), () => { new ChannelOption("abc", null); });
        }

        [Test]
        public void CreateChannelArgsNull()
        {
            var channelArgs = ChannelOptions.CreateChannelArgs(null);
            Assert.IsTrue(channelArgs.IsInvalid);
        }

        [Test]
        public void CreateChannelArgsEmpty()
        {
            var options = new List<ChannelOption>();
            var channelArgs = ChannelOptions.CreateChannelArgs(options);
            channelArgs.Dispose();
        }

        [Test]
        public void CreateChannelArgs()
        {
            var options = new List<ChannelOption>
            {
                new ChannelOption("ABC", "XYZ"),
                new ChannelOption("somename", "IJKLM"),
                new ChannelOption("intoption", 12345),
                new ChannelOption("GHIJK", 12345),
            };

            var channelArgs = ChannelOptions.CreateChannelArgs(options);
            channelArgs.Dispose();
        }
    }
}
