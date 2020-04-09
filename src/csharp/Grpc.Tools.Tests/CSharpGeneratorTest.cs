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

using System;
using System.IO;
using System.Text;
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
        [TestCase("one.two.proto", "OneTwo.cs", "OneTwoGrpc.cs")]
        [TestCase("one123two.proto", "One123Two.cs", "One123TwoGrpc.cs")]
        [TestCase("__one_two!.proto", "OneTwo.cs", "OneTwoGrpc.cs")]
        [TestCase("one(two).proto", "OneTwo.cs", "OneTwoGrpc.cs")]
        [TestCase("one_(two).proto", "OneTwo.cs", "OneTwoGrpc.cs")]
        [TestCase("one two.proto", "OneTwo.cs", "OneTwoGrpc.cs")]
        [TestCase("one_ two.proto", "OneTwo.cs", "OneTwoGrpc.cs")]
        [TestCase("one .proto", "One.cs", "OneGrpc.cs")]
        public void NameMangling(string proto, string expectCs, string expectGrpcCs)
        {
            var poss = _generator.GetPossibleOutputs(Utils.MakeItem(proto, "grpcservices", "both"));
            Assert.AreEqual(2, poss.Length);
            Assert.Contains(expectCs, poss);
            Assert.Contains(expectGrpcCs, poss);
        }

        private static object[] s_namespaceManglingTests = {
            new object[] {"none.proto", "None", "None.cs"},
            new object[] {"package.proto", "Package", "Helloworld" + Path.DirectorySeparatorChar + "Package.cs"},
            new object[] {"namespace.proto", "Namespace", "Google" + Path.DirectorySeparatorChar + "Protobuf" + Path.DirectorySeparatorChar + "TestProtos" + Path.DirectorySeparatorChar + "Namespace.cs"},
            new object[] {"namespace_package.proto", "NamespacePackage", "Google" + Path.DirectorySeparatorChar + "Protobuf" + Path.DirectorySeparatorChar + "TestProtos" + Path.DirectorySeparatorChar + "NamespacePackage.cs"}
        };
        [TestCaseSource(nameof(s_namespaceManglingTests))]
        public void NamespaceMangling(string proto, string context, string expectNamespace)
        {
            StringBuilder content = new StringBuilder();
            content.Append(Testfiles.Header);
            content.Append(Environment.NewLine);
            content.Append(Testfiles.ResourceManager.GetString(context));
            content.Append(Environment.NewLine);
            content.Append(Testfiles.Content);

            File.WriteAllText(proto, content.ToString());
            var poss = _generator.GetPossibleOutputs(Utils.MakeItem(proto, "basenamespaceenabled", "true"));
            Assert.AreEqual(1, poss.Length);
            Assert.Contains(expectNamespace, poss);
        }

        private static object[] s_baseNamespaceManglingTests = {
            new object[] {"none.proto", "None", "", "None.cs"},
            new object[] {"package.proto", "Package", "Helloworld", "Package.cs"},
            new object[] {"package.proto", "Package", "", "Helloworld" + Path.DirectorySeparatorChar + "Package.cs"},
            new object[] {"namespace.proto", "Namespace", "Google.Protobuf.TestProtos", "Namespace.cs"},
            new object[] {"namespace.proto", "Namespace", "Google", "Protobuf" + Path.DirectorySeparatorChar + "TestProtos" + Path.DirectorySeparatorChar + "Namespace.cs"},
            new object[] {"namespace_package.proto", "NamespacePackage", "Google.Protobuf.TestProtos", "NamespacePackage.cs"},
            new object[] {"namespace_package.proto", "NamespacePackage", "Google.Protobuf", "TestProtos" + Path.DirectorySeparatorChar + "NamespacePackage.cs"}
        };
        [TestCaseSource(nameof(s_baseNamespaceManglingTests))]
        public void BaseNamespaceMangling(string proto, string context, string baseNamespace, string expectNamespace)
        {
            StringBuilder content = new StringBuilder();
            content.Append(Testfiles.Header);
            content.Append(Environment.NewLine);
            content.Append(Testfiles.ResourceManager.GetString(context));
            content.Append(Environment.NewLine);
            content.Append(Testfiles.Content);

            File.WriteAllText(proto, content.ToString());
            var poss = _generator.GetPossibleOutputs(Utils.MakeItem(proto, "basenamespaceenabled", "true", "basenamespace", baseNamespace));
            Assert.AreEqual(1, poss.Length);
            Assert.Contains(expectNamespace, poss);
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
    };
}
