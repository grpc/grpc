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
using Microsoft.Build.Framework;
using Moq;
using NUnit.Framework;

namespace Grpc.Tools.Tests
{
    public class ProtoCompileCommandLineGeneratorTest : ProtoCompileBasicTest
    {
        [SetUp]
        public new void SetUp()
        {
            _task.Generator = "csharp";
            _task.OutputDir = "outdir";
            _task.Protobuf = Utils.MakeSimpleItems("a.proto");
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

        [Test]
        public void MinimalCompile()
        {
            ExecuteExpectSuccess();
            Assert.That(_task.LastPathToTool, Does.Match(@"protoc(.exe)?$"));
            Assert.That(_task.LastResponseFile, Is.EqualTo(new[] {
                "--csharp_out=outdir", "--error_format=msvs", "a.proto" }));
        }

        [Test]
        public void CompileTwoFiles()
        {
            _task.Protobuf = Utils.MakeSimpleItems("a.proto", "foo/b.proto");
            ExecuteExpectSuccess();
            Assert.That(_task.LastResponseFile, Is.EqualTo(new[] {
                "--csharp_out=outdir", "--error_format=msvs", "a.proto", "foo/b.proto" }));
        }

        [Test]
        public void CompileWithProtoPaths()
        {
            _task.ProtoPath = new[] { "/path1", "/path2" };
            ExecuteExpectSuccess();
            Assert.That(_task.LastResponseFile, Is.EqualTo(new[] {
                "--csharp_out=outdir", "--proto_path=/path1",
                "--proto_path=/path2", "--error_format=msvs", "a.proto" }));
        }

        [TestCase("Cpp")]
        [TestCase("CSharp")]
        [TestCase("Java")]
        [TestCase("Javanano")]
        [TestCase("Js")]
        [TestCase("Objc")]
        [TestCase("Php")]
        [TestCase("Python")]
        [TestCase("Ruby")]
        public void CompileWithOptions(string gen)
        {
            _task.Generator = gen;
            _task.OutputOptions = new[] { "foo", "bar" };
            ExecuteExpectSuccess();
            gen = gen.ToLowerInvariant();
            Assert.That(_task.LastResponseFile, Is.EqualTo(new[] {
                $"--{gen}_out=outdir", $"--{gen}_opt=foo,bar", "--error_format=msvs", "a.proto" }));
        }

        [Test]
        public void OutputDependencyFile()
        {
            _task.DependencyOut = "foo/my.protodep";
            // Task fails trying to read the non-generated file; we ignore that.
            _task.Execute();
            Assert.That(_task.LastResponseFile,
                Does.Contain("--dependency_out=foo/my.protodep"));
        }

        [Test]
        public void OutputDependencyWithProtoDepDir()
        {
            _task.ProtoDepDir = "foo";
            // Task fails trying to read the non-generated file; we ignore that.
            _task.Execute();
            Assert.That(_task.LastResponseFile,
                Has.One.Match(@"^--dependency_out=foo[/\\].+_a.protodep$"));
        }

        [Test]
        public void GenerateGrpc()
        {
            _task.GrpcPluginExe = "/foo/grpcgen";
            ExecuteExpectSuccess();
            Assert.That(_task.LastResponseFile, Is.SupersetOf(new[] {
                "--csharp_out=outdir", "--grpc_out=outdir",
                "--plugin=protoc-gen-grpc=/foo/grpcgen" }));
        }

        [Test]
        public void GenerateGrpcWithOutDir()
        {
            _task.GrpcPluginExe = "/foo/grpcgen";
            _task.GrpcOutputDir = "gen-out";
            ExecuteExpectSuccess();
            Assert.That(_task.LastResponseFile, Is.SupersetOf(new[] {
                "--csharp_out=outdir", "--grpc_out=gen-out" }));
        }

        [Test]
        public void GenerateGrpcWithOptions()
        {
            _task.GrpcPluginExe = "/foo/grpcgen";
            _task.GrpcOutputOptions = new[] { "baz", "quux" };
            ExecuteExpectSuccess();
            Assert.That(_task.LastResponseFile,
                        Does.Contain("--grpc_opt=baz,quux"));
        }

        [Test]
        public void AdditionalProtocArguments()
        {
            _task.AdditionalProtocArguments = new[] { "--experimental_allow_proto3_optional" };
            ExecuteExpectSuccess();
            Assert.That(_task.LastResponseFile,
                Does.Contain("--experimental_allow_proto3_optional"));
        }

        [Test]
        public void DirectoryArgumentsSlashTrimmed()
        {
            _task.GrpcPluginExe = "/foo/grpcgen";
            _task.GrpcOutputDir = "gen-out/";
            _task.OutputDir = "outdir/";
            _task.ProtoPath = new[] { "/path1/", "/path2/" };
            ExecuteExpectSuccess();
            Assert.That(_task.LastResponseFile, Is.SupersetOf(new[] {
        "--proto_path=/path1", "--proto_path=/path2",
        "--csharp_out=outdir", "--grpc_out=gen-out" }));
        }

        [TestCase(".", ".")]
        [TestCase("/", "/")]
        [TestCase("//", "/")]
        [TestCase("/foo/", "/foo")]
        [TestCase("/foo", "/foo")]
        [TestCase("foo/", "foo")]
        [TestCase("foo//", "foo")]
        [TestCase("foo/\\", "foo")]
        [TestCase("foo\\/", "foo")]
        [TestCase("C:\\foo", "C:\\foo")]
        [TestCase("C:", "C:")]
        [TestCase("C:\\", "C:\\")]
        [TestCase("C:\\\\", "C:\\")]
        public void DirectorySlashTrimmingCases(string given, string expect)
        {
            if (Path.DirectorySeparatorChar == '/')
                expect = expect.Replace('\\', '/');
            _task.OutputDir = given;
            ExecuteExpectSuccess();
            Assert.That(_task.LastResponseFile,
                        Does.Contain("--csharp_out=" + expect));
        }

        [TestCase(
            "../Protos/greet.proto(19) : warning in column=5 : warning : When enum name is stripped and label is PascalCased (Zero) this value label conflicts with Zero.",
            "../Protos/greet.proto",
            19,
            5,
            "warning : When enum name is stripped and label is PascalCased (Zero) this value label conflicts with Zero.")]
        [TestCase(
            "../Protos/greet.proto: warning: Import google/protobuf/empty.proto but not used.",
            "../Protos/greet.proto",
            0,
            0,
            "Import google/protobuf/empty.proto but not used.")]
        [TestCase("../Protos/greet.proto(14) : error in column=10: \"name\" is already defined in \"Greet.HelloRequest\".", null, 0, 0, null)]
        [TestCase("../Protos/greet.proto: Import \"google / protobuf / empty.proto\" was listed twice.", null, 0, 0, null)]
        public void WarningsParsed(string stderr, string file, int line, int col, string message)
        {
            _task.StdErrMessages.Add(stderr);

            _mockEngine
                .Setup(me => me.LogWarningEvent(It.IsAny<BuildWarningEventArgs>()))
                .Callback((BuildWarningEventArgs e) => {
                    if (file != null)
                    {
                        Assert.AreEqual(file, e.File);
                        Assert.AreEqual(line, e.LineNumber);
                        Assert.AreEqual(col, e.ColumnNumber);
                        Assert.AreEqual(message, e.Message);
                    }
                    else
                    {
                        Assert.Fail($"Error logged by build engine:\n{e.Message}");
                    }
                });

            bool result = _task.Execute();
            Assert.IsFalse(result);
        }

        [TestCase(
            "../Protos/greet.proto(14) : error in column=10: \"name\" is already defined in \"Greet.HelloRequest\".",
            "../Protos/greet.proto",
            14,
            10,
            "\"name\" is already defined in \"Greet.HelloRequest\".")]
        [TestCase(
            "../Protos/greet.proto: Import \"google / protobuf / empty.proto\" was listed twice.",
            "../Protos/greet.proto",
            0,
            0,
            "Import \"google / protobuf / empty.proto\" was listed twice.")]
        [TestCase("../Protos/greet.proto(19) : warning in column=5 : warning : When enum name is stripped and label is PascalCased (Zero) this value label conflicts with Zero.", null, 0, 0, null)]
        [TestCase("../Protos/greet.proto: warning: Import google/protobuf/empty.proto but not used.", null, 0, 0, null)]
        public void ErrorsParsed(string stderr, string file, int line, int col, string message)
        {
            _task.StdErrMessages.Add(stderr);

            _mockEngine
                .Setup(me => me.LogErrorEvent(It.IsAny<BuildErrorEventArgs>()))
                .Callback((BuildErrorEventArgs e) => {
                    if (file != null)
                    {
                        Assert.AreEqual(file, e.File);
                        Assert.AreEqual(line, e.LineNumber);
                        Assert.AreEqual(col, e.ColumnNumber);
                        Assert.AreEqual(message, e.Message);
                    }
                    else
                    {
                        // Ignore expected error
                        // "protoc/protoc.exe" existed with code -1.
                        if (!e.Message.EndsWith("exited with code -1."))
                        {
                            Assert.Fail($"Error logged by build engine:\n{e.Message}");
                        }
                    }
                });

            bool result = _task.Execute();
            Assert.IsFalse(result);
        }
    };
}
