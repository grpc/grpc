#region Copyright notice and license

// Copyright 2015, Google Inc.
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
