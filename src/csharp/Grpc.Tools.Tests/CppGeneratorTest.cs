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

using System.IO;
using NUnit.Framework;

namespace Grpc.Tools.Tests
{
    public class CppGeneratorTest : GeneratorTest
    {
        GeneratorServices _generator;

        [SetUp]
        public new void SetUp()
        {
            _generator = GeneratorServices.GetForLanguage("Cpp", _log);
        }

        [TestCase("foo.proto", "", "foo")]
        [TestCase("foo.proto", ".", "foo")]
        [TestCase("foo.proto", "./", "foo")]
        [TestCase("sub/foo.proto", "", "sub/foo")]
        [TestCase("root/sub/foo.proto", "root", "sub/foo")]
        [TestCase("root/sub/foo.proto", "root", "sub/foo")]
        [TestCase("/root/sub/foo.proto", "/root", "sub/foo")]
        public void RelativeDirectoryCompute(string proto, string root, string expectStem)
        {
            if (Path.DirectorySeparatorChar == '\\')
                expectStem = expectStem.Replace('/', '\\');
            var poss = _generator.GetPossibleOutputs(Utils.MakeItem(proto, "ProtoRoot", root));
            Assert.AreEqual(2, poss.Length);
            Assert.Contains(expectStem + ".pb.cc", poss);
            Assert.Contains(expectStem + ".pb.h", poss);
        }

        [Test]
        public void NoGrpcTwoOutputs()
        {
            var poss = _generator.GetPossibleOutputs(Utils.MakeItem("foo.proto"));
            Assert.AreEqual(2, poss.Length);
        }

        [TestCase("false")]
        [TestCase("")]
        public void GrpcDisabledTwoOutput(string grpc)
        {
            var item = Utils.MakeItem("foo.proto", "grpcservices", grpc);
            var poss = _generator.GetPossibleOutputs(item);
            Assert.AreEqual(2, poss.Length);
        }

        [TestCase("true")]
        public void GrpcEnabledFourOutputs(string grpc)
        {
            var item = Utils.MakeItem("foo.proto", "grpcservices", grpc);
            var poss = _generator.GetPossibleOutputs(item);
            Assert.AreEqual(4, poss.Length);
            Assert.Contains("foo.pb.cc", poss);
            Assert.Contains("foo.pb.h", poss);
            Assert.Contains("foo.grpc.pb.cc", poss);
            Assert.Contains("foo.grpc.pb.h", poss);
        }

        [Test]
        public void OutputDirMetadataRecognized()
        {
            var item = Utils.MakeItem("foo.proto", "OutputDir", "out");
            var poss = _generator.GetPossibleOutputs(item);
            Assert.AreEqual(2, poss.Length);
            Assert.That(Path.GetDirectoryName(poss[0]), Is.EqualTo("out"));
        }
    };
}
