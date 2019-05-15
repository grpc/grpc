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

using System.Collections.Generic;
using System.Linq;
using System.Reflection;  // UWYU: Object.GetType() extension.
using Microsoft.Build.Framework;
using Moq;
using NUnit.Framework;

namespace Grpc.Tools.Tests
{
    public class ProtoCompileBasicTest
    {
        // Mock task class that stops right before invoking protoc.
        public class ProtoCompileTestable : ProtoCompile
        {
            public string LastPathToTool { get; private set; }
            public string[] LastResponseFile { get; private set; }
            public List<string> StdErrMessages { get; } = new List<string>();

            protected override int ExecuteTool(string pathToTool,
                                               string response,
                                               string commandLine)
            {
                // We should never be using command line commands.
                Assert.That(commandLine, Is.Null | Is.Empty);

                // Must receive a path to tool
                Assert.That(pathToTool, Is.Not.Null & Is.Not.Empty);
                Assert.That(response, Is.Not.Null & Does.EndWith("\n"));

                LastPathToTool = pathToTool;
                LastResponseFile = response.Remove(response.Length - 1).Split('\n');

                foreach (string message in StdErrMessages)
                {
                    LogEventsFromTextOutput(message, MessageImportance.High);
                }

                // Do not run the tool, but pretend it ran successfully.
                return StdErrMessages.Any() ? -1 : 0;
            }
        };

        protected Mock<IBuildEngine> _mockEngine;
        protected ProtoCompileTestable _task;

        [SetUp]
        public void SetUp()
        {
            _mockEngine = new Mock<IBuildEngine>();
            _task = new ProtoCompileTestable {
                BuildEngine = _mockEngine.Object
            };
        }

        [TestCase("Protobuf")]
        [TestCase("Generator")]
        [TestCase("OutputDir")]
        [Description("We trust MSBuild to initialize these properties.")]
        public void RequiredAttributePresentOnProperty(string prop)
        {
            var pinfo = _task.GetType()?.GetProperty(prop);
            Assert.NotNull(pinfo);
            Assert.That(pinfo, Has.Attribute<RequiredAttribute>());
        }
    };
}
