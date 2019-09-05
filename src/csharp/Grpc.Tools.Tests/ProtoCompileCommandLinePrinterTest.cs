#region Copyright notice and license

// Copyright 2018 gRPC authors.
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

using Microsoft.Build.Framework;
using Moq;
using NUnit.Framework;

namespace Grpc.Tools.Tests
{
    public class ProtoCompileCommandLinePrinterTest : ProtoCompileBasicTest
    {
        [SetUp]
        public new void SetUp()
        {
            _task.Generator = "csharp";
            _task.OutputDir = "outdir";
            _task.Protobuf = Utils.MakeSimpleItems("a.proto");

            _mockEngine
              .Setup(me => me.LogMessageEvent(It.IsAny<BuildMessageEventArgs>()))
              .Callback((BuildMessageEventArgs e) =>
                  Assert.Fail($"Error logged by build engine:\n{e.Message}"))
              .Verifiable("Command line was not output by the task.");
        }

        void ExecuteExpectSuccess()
        {
            _mockEngine
              .Setup(me => me.LogErrorEvent(It.IsAny<BuildErrorEventArgs>()))
              .Callback((BuildErrorEventArgs e) =>
                  Assert.Fail($"Error logged by build engine:\n{e.Message}"));
            bool result = _task.Execute();
            Assert.IsTrue(result);
        }
    };
}
