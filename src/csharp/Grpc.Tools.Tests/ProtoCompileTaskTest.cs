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

using System.Reflection;
using Microsoft.Build.Framework;
using Moq;
using NUnit.Framework;

namespace Grpc.Tools.Tests {
  public class ProtoCompileBasicTests {
    // Mock task class that stops right before invoking protoc.
    public class ProtoCompileTestable : ProtoCompile {
      public string LastPathToTool { get; private set; }
      public string[] LastResponseFile { get; private set; }

      protected override int ExecuteTool(string pathToTool,
                                         string response,
                                         string commandLine) {
        // We should never be using command line commands.
        Assert.That(commandLine, Is.Null | Is.Empty);

        // Must receive a path to tool
        Assert.That(pathToTool, Is.Not.Null & Is.Not.Empty);
        Assert.That(response, Is.Not.Null & Does.EndWith("\n"));

        LastPathToTool = pathToTool;
        LastResponseFile = response.Remove(response.Length - 1).Split('\n');

        // Do not run the tool, but pretend it ran successfully.
        return 0;
      }
    };

    protected Mock<IBuildEngine> _mockEngine;
    protected ProtoCompileTestable _task;

    [SetUp]
    public void SetUp() {
      _mockEngine = new Mock<IBuildEngine>();
      _task = new ProtoCompileTestable {
        BuildEngine = _mockEngine.Object
      };
    }

    [TestCase("ProtoBuf")]
    [TestCase("Generator")]
    [TestCase("OutputDir")]
    [Description("We trust MSBuild to initialize these properties.")]
    public void RequiredAttributePresentOnProperty(string prop) {
      var pinfo = _task.GetType()?.GetProperty(prop);
      Assert.NotNull(pinfo);
      Assert.That(pinfo, Has.Attribute<RequiredAttribute>());
    }
  };

  internal class ProtoCompileCommandLineGeneratorTests : ProtoCompileBasicTests {
    [SetUp]
    public new void SetUp() {
      _task.Generator = "csharp";
      _task.OutputDir = "outdir";
      _task.ProtoBuf = Utils.MakeSimpleItems("a.proto");
    }

    void ExecuteExpectSuccess() {
      _mockEngine
        .Setup(me => me.LogErrorEvent(It.IsAny<BuildErrorEventArgs>()))
        .Callback((BuildErrorEventArgs e) =>
            Assert.Fail($"Error logged by build engine:\n{e.Message}"));
      bool result = _task.Execute();
      Assert.IsTrue(result);
    }

    [Test]
    public void MinimalCompile() {
      ExecuteExpectSuccess();
      Assert.That(_task.LastPathToTool, Does.Match(@"protoc(.exe)?$"));
      Assert.That(_task.LastResponseFile, Is.EqualTo(new[] {
        "--csharp_out=outdir", "a.proto" }));
    }

    [Test]
    public void CompileTwoFiles() {
      _task.ProtoBuf = Utils.MakeSimpleItems("a.proto", "foo/b.proto");
      ExecuteExpectSuccess();
      Assert.That(_task.LastResponseFile, Is.EqualTo(new[] {
        "--csharp_out=outdir", "a.proto", "foo/b.proto" }));
    }

    [Test]
    public void CompileWithProtoPaths() {
      _task.ProtoPath = new[] { "/path1", "/path2" };
      ExecuteExpectSuccess();
      Assert.That(_task.LastResponseFile, Is.EqualTo(new[] {
        "--csharp_out=outdir", "--proto_path=/path1",
        "--proto_path=/path2", "a.proto" }));
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
    public void CompileWithOptions(string gen) {
      _task.Generator = gen;
      _task.OutputOptions = new[] { "foo", "bar" };
      ExecuteExpectSuccess();
      gen = gen.ToLowerInvariant();
      Assert.That(_task.LastResponseFile, Is.EqualTo(new[] {
        $"--{gen}_out=outdir", $"--{gen}_opt=foo,bar", "a.proto" }));
    }

    [Test]
    public void OutputDependencyFile() {
      _task.DependencyOut = "foo/my.protodep";
      // Task fails trying to read the non-generated file; we ignore that.
      _task.Execute();
      Assert.That(_task.LastResponseFile,
        Does.Contain("--dependency_out=foo/my.protodep"));
    }

    [Test]
    public void OutputDependencyWithProtoDepDir() {
      _task.ProtoDepDir = "foo";
      // Task fails trying to read the non-generated file; we ignore that.
      _task.Execute();
      Assert.That(_task.LastResponseFile,
        Has.One.Match(@"^--dependency_out=foo[/\\].+_a.protodep$"));
    }

    [Test]
    public void GenerateGrpc() {
      _task.GrpcPluginExe = "/foo/grpcgen";
      ExecuteExpectSuccess();
      Assert.That(_task.LastResponseFile, Is.SupersetOf(new[] {
        "--csharp_out=outdir", "--grpc_out=outdir",
        "--plugin=protoc-gen-grpc=/foo/grpcgen" }));
    }

    [Test]
    public void GenerateGrpcWithOutDir() {
      _task.GrpcPluginExe = "/foo/grpcgen";
      _task.GrpcOutputDir = "gen-out";
      ExecuteExpectSuccess();
      Assert.That(_task.LastResponseFile, Is.SupersetOf(new[] {
        "--csharp_out=outdir", "--grpc_out=gen-out" }));
    }

    [Test]
    public void GenerateGrpcWithOptions() {
      _task.GrpcPluginExe = "/foo/grpcgen";
      _task.GrpcOutputOptions = new[] { "baz", "quux" };
      ExecuteExpectSuccess();
      Assert.That(_task.LastResponseFile,
                  Does.Contain("--grpc_opt=baz,quux"));
    }

    [Test]
    public void DirectoryArgumentsSlashTrimmed() {
      _task.GrpcPluginExe = "/foo/grpcgen";
      _task.GrpcOutputDir = "gen-out/";
      _task.OutputDir = "outdir/";
      _task.ProtoPath = new[] { "/path1/", "/path2/" };
      ExecuteExpectSuccess();
      Assert.That(_task.LastResponseFile, Is.SupersetOf(new[] {
        "--proto_path=/path1", "--proto_path=/path2",
        "--csharp_out=outdir", "--grpc_out=gen-out" }));
    }

    [TestCase("."      , ".")]
    [TestCase("/"      , "/")]
    [TestCase("//"     , "/")]
    [TestCase("/foo/"  , "/foo")]
    [TestCase("/foo"   , "/foo")]
    [TestCase("foo/"   , "foo")]
    [TestCase("foo//"  , "foo")]
    [TestCase("foo/\\" , "foo")]
    [TestCase("foo\\/" , "foo")]
    [TestCase("C:\\foo", "C:\\foo")]
    [TestCase("C:"     , "C:")]
    [TestCase("C:\\"   , "C:\\")]
    [TestCase("C:\\\\" , "C:\\")]
    public void DirectorySlashTrimmingCases(string given, string expect) {
      _task.OutputDir = given;
      ExecuteExpectSuccess();
      Assert.That(_task.LastResponseFile,
                  Does.Contain("--csharp_out=" + expect));
    }
  };

  internal class ProtoCompileCommandLinePrinterTests : ProtoCompileBasicTests {
    [SetUp]
    public new void SetUp() {
      _task.Generator = "csharp";
      _task.OutputDir = "outdir";
      _task.ProtoBuf = Utils.MakeSimpleItems("a.proto");

      _mockEngine
        .Setup(me => me.LogMessageEvent(It.IsAny<BuildMessageEventArgs>()))
        .Callback((BuildMessageEventArgs e) =>
            Assert.Fail($"Error logged by build engine:\n{e.Message}"))
        .Verifiable("Command line was not output by the task.");
    }

    void ExecuteExpectSuccess() {
      _mockEngine
        .Setup(me => me.LogErrorEvent(It.IsAny<BuildErrorEventArgs>()))
        .Callback((BuildErrorEventArgs e) =>
            Assert.Fail($"Error logged by build engine:\n{e.Message}"));
      bool result = _task.Execute();
      Assert.IsTrue(result);
    }
  };
}
