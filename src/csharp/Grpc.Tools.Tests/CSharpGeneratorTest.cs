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

using NUnit.Framework;

namespace Grpc.Tools.Tests
{
    public class CSharpGeneratorTest : GeneratorTest
    {
        GeneratorServices _generator;

        [SetUp]
        public new void SetUp()
        {
            _generator = GeneratorServices.GetForLanguage("CSharp", _log);
        }

        [TestCase("foo.proto", "Foo.cs", "FooGrpc.cs")]
        [TestCase("sub/foo.proto", "Foo.cs", "FooGrpc.cs")]
        [TestCase("one_two.proto", "OneTwo.cs", "OneTwoGrpc.cs")]
        [TestCase("ONE_TWO.proto", "ONETWO.cs", "ONETWOGrpc.cs")]
        [TestCase("one.two.proto", "OneTwo.cs", "One.twoGrpc.cs")]
        [TestCase("one123two.proto", "One123Two.cs", "One123twoGrpc.cs")]
        [TestCase("__one_two!.proto", "OneTwo.cs", "OneTwo!Grpc.cs")]
        [TestCase("one(two).proto", "OneTwo.cs", "One(two)Grpc.cs")]
        [TestCase("one_(two).proto", "OneTwo.cs", "One(two)Grpc.cs")]
        [TestCase("one two.proto", "OneTwo.cs", "One twoGrpc.cs")]
        [TestCase("one_ two.proto", "OneTwo.cs", "One twoGrpc.cs")]
        [TestCase("one .proto", "One.cs", "One Grpc.cs")]
        public void NameMangling(string proto, string expectCs, string expectGrpcCs)
        {
            var poss = _generator.GetPossibleOutputs(Utils.MakeItem(proto, "grpcservices", "both"));
            Assert.AreEqual(2, poss.Length);
            Assert.Contains(expectCs, poss);
            Assert.Contains(expectGrpcCs, poss);
        }

        [Test]
        public void NoGrpcOneOutput()
        {
            var poss = _generator.GetPossibleOutputs(Utils.MakeItem("foo.proto"));
            Assert.AreEqual(1, poss.Length);
        }

        [TestCase("none")]
        [TestCase("")]
        public void GrpcNoneOneOutput(string grpc)
        {
            var item = Utils.MakeItem("foo.proto", "grpcservices", grpc);
            var poss = _generator.GetPossibleOutputs(item);
            Assert.AreEqual(1, poss.Length);
        }

        [TestCase("client")]
        [TestCase("server")]
        [TestCase("both")]
        public void GrpcEnabledTwoOutputs(string grpc)
        {
            var item = Utils.MakeItem("foo.proto", "grpcservices", grpc);
            var poss = _generator.GetPossibleOutputs(item);
            Assert.AreEqual(2, poss.Length);
        }

        [Test]
        public void OutputDirMetadataRecognized()
        {
            var item = Utils.MakeItem("foo.proto", "OutputDir", "out");
            var poss = _generator.GetPossibleOutputs(item);
            Assert.AreEqual(1, poss.Length);
            Assert.That(poss[0], Is.EqualTo("out/Foo.cs") | Is.EqualTo("out\\Foo.cs"));
        }

        [Test]
        public void OutputDirPatched()
        {
            var item = Utils.MakeItem("sub/foo.proto", "OutputDir", "out");
            var output = _generator.PatchOutputDirectory(item);
            var poss = _generator.GetPossibleOutputs(output);
            Assert.AreEqual(1, poss.Length);
            Assert.That(poss[0], Is.EqualTo("out/sub/Foo.cs") | Is.EqualTo("out\\sub\\Foo.cs"));
        }
    };
}
