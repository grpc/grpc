#region Copyright notice and license

// Copyright 2022 The gRPC Authors
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
using NUnit.Framework;
using System.Diagnostics;
using System.Reflection;
using System.Collections.Specialized;
using System.Collections;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using Newtonsoft.Json;

namespace Grpc.Tools.Tests
{
    /// <summary>
    /// Tests for Grpc.Tools MSBuild .target and .props files.
    /// </summary>
    /// <remarks>
    /// The Grpc.Tools NuGet package is not tested directly, but instead the
    /// same .target and .props files are included in a MSBuild project and
    /// that project is built using "dotnet build" with the SDK installed on
    /// the test machine.
    /// <para>
    /// The real protoc compiler is not called. Instead a fake protoc script is
    /// called that does the minimum work needed for the build to succeed
    /// (generating cs files and writing dependencies file) and also writes out
    /// the arguments it was called with in a JSON file. The output is checked
    /// with expected results.
    /// </para>
    /// </remarks>
    [TestFixture]
    public class MsBuildIntegrationTest
    {
        private const string TASKS_ASSEMBLY_PROPERTY = "_Protobuf_MsBuildAssembly";
        private const string TASKS_ASSEMBLY_DLL = "Protobuf.MSBuild.dll";
        private const string PROTBUF_FULLPATH_PROPERTY = "Protobuf_ProtocFullPath";
        private const string PLUGIN_FULLPATH_PROPERTY = "gRPC_PluginFullPath";
        private const string TOOLS_BUILD_DIR_PROPERTY = "GrpcToolsBuildDir";

        private const string MSBUILD_LOG_VERBOSITY = "diagnostic"; // "diagnostic" or "detailed"

        private string testId;
        private string fakeProtoc;
        private string grpcToolsBuildDir;
        private string tasksAssembly;
        private string testDataDir;
        private string testProjectDir;
        private string testOutBaseDir;
        private string testOutDir;

        [SetUp]
        public void InitTest()
        {
#if NET45
            // We need to run these tests for one framework.
            // This test class is just a driver for calling the
            // "dotnet build" processes, so it doesn't matter what
            // the runtime of this class actually is.
            Assert.Ignore("Skipping test when NET45");
#endif
        }

        [Test]
        public void TestSingleProto()
        {
            SetUpForTest(nameof(TestSingleProto));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("file.proto", "File.cs", "FileGrpc.cs");

            TryRunMsBuild("TestSingleProto", expectedFiles.ToString());
        }

        [Test]
        public void TestMultipleProtos()
        {
            SetUpForTest(nameof(TestMultipleProtos));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("file.proto", "File.cs", "FileGrpc.cs")
                .Add("protos/another.proto", "Another.cs", "AnotherGrpc.cs")
                .Add("second.proto", "Second.cs", "SecondGrpc.cs")
                // Test duplicate name under different directories is allowed.
                // See https://github.com/grpc/grpc/issues/17672
                .Add("protos/file.proto", "File.cs", "FileGrpc.cs"); 

            TryRunMsBuild("TestMultipleProtos", expectedFiles.ToString());
        }

        [Test]
        public void TestAtInPath()
        {
            SetUpForTest(nameof(TestAtInPath));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("@protos/file.proto", "File.cs", "FileGrpc.cs");

            TryRunMsBuild("TestAtInPath", expectedFiles.ToString());
        }

        [Test]
        public void TestProtoOutsideProject()
        {
            SetUpForTest(nameof(TestProtoOutsideProject), "TestProtoOutsideProject/project");

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("../api/greet.proto", "Greet.cs", "GreetGrpc.cs");

            TryRunMsBuild("TestProtoOutsideProject/project", expectedFiles.ToString());
        }

        [Test]
        public void TestCharactersInName()
        {
            // see https://github.com/grpc/grpc/issues/17661 - dot in name
            // and https://github.com/grpc/grpc/issues/18698 - numbers in name
            SetUpForTest(nameof(TestCharactersInName));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("protos/hello.world.proto", "HelloWorld.cs", "Hello.worldGrpc.cs");
            expectedFiles.Add("protos/m_double_2d.proto", "MDouble2D.cs", "MDouble2dGrpc.cs");

            TryRunMsBuild("TestCharactersInName", expectedFiles.ToString());
        }

        [Test]
        public void TestExtraOptions()
        {
            // Test various extra options passed to protoc and plugin
            // See https://github.com/grpc/grpc/issues/25950
            // Tests setting AdditionalProtocArguments, OutputOptions and GrpcOutputOptions
            SetUpForTest(nameof(TestExtraOptions));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("file.proto", "File.cs", "FileGrpc.cs");

            TryRunMsBuild("TestExtraOptions", expectedFiles.ToString());
        }

        [Test]
        public void TestGrpcServicesMetadata()
        {
            // Test different values for GrpcServices item metadata
            SetUpForTest(nameof(TestGrpcServicesMetadata));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("messages.proto", "Messages.cs");
            expectedFiles.Add("serveronly.proto", "Serveronly.cs", "ServeronlyGrpc.cs");
            expectedFiles.Add("clientonly.proto", "Clientonly.cs", "ClientonlyGrpc.cs");
            expectedFiles.Add("clientandserver.proto", "Clientandserver.cs", "ClientandserverGrpc.cs");

            TryRunMsBuild("TestGrpcServicesMetadata", expectedFiles.ToString());
        }

        [Test]
        public void TestSetOutputDirs()
        {
            // Test setting different GrpcOutputDir and OutputDir
            SetUpForTest(nameof(TestSetOutputDirs));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("file.proto", "File.cs", "FileGrpc.cs");

            TryRunMsBuild("TestSetOutputDirs", expectedFiles.ToString());
        }

        [Test]
        public void TestNullable()
        {
            // Test code generation options for nullable reference types
            // when project contains <Nullable>enable</Nullable>
            SetUpForTest(nameof(TestNullable));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("file.proto", "File.cs", "FileGrpc.cs");

            TryRunMsBuild("TestNullable", expectedFiles.ToString());
        }

        [Test]
        public void TestNullableForceOff()
        {
            // Test code generation options for nullable reference types
            // when project contains <Nullable>enable</Nullable> but
            // it is force off by declaring other properties
            SetUpForTest(nameof(TestNullableForceOff));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("file.proto", "File.cs", "FileGrpc.cs");

            TryRunMsBuild("TestNullableForceOff", expectedFiles.ToString());
        }

        [Test]
        public void TestNullableForceOn()
        {
            // Test code generation options for nullable reference types
            // when project contains <Nullable>disable</Nullable> but
            // it is forced on by declaring other properties
            SetUpForTest(nameof(TestNullableForceOn));

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("file.proto", "File.cs", "FileGrpc.cs");

            TryRunMsBuild("TestNullableForceOn", expectedFiles.ToString());
        }

        /// <summary>
        /// Set up common paths for all the tests
        /// </summary>
        private void SetUpCommonPaths()
        {
            var assemblyDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            testDataDir = Path.GetFullPath($"{assemblyDir}/../../../IntegrationTests");

            // Path for fake proto.
            // On Windows we have to wrap the python script in a BAT script since we can only
            // pass one executable name without parameters to the MSBuild
            // - e.g. we can't give "python fakeprotoc.py"
            var fakeProtocScript = Platform.IsWindows ? "fakeprotoc.bat" : "fakeprotoc.py";
            fakeProtoc = Path.GetFullPath($"{assemblyDir}/../../../scripts/{fakeProtocScript}");

            // Path for "build" directory under Grpc.Tools
            grpcToolsBuildDir = Path.GetFullPath($"{assemblyDir}/../../../../Grpc.Tools/build");

            // Task assembly is needed to run the extension tasks
            // We use the assembly that was copied next to Grpc.Tools.Tests.dll
            // as a Grpc.Tools.Tests dependency since we know it's the correct one
            // and we don't have to figure out its original path (which is different
            // for debug/release builds etc).
            tasksAssembly = Path.Combine(assemblyDir, TASKS_ASSEMBLY_DLL);

            // put test ouptput directory outside of Grpc.Tools.Tests to avoid problems with
            // repeated builds.
            testOutBaseDir = NormalizePath(Path.GetFullPath($"{assemblyDir}/../../../../test-out/grpc_tools_integration_tests"));
        }


        /// <summary>
        /// Normalize path string to use just forward slashes. That makes it easier to compare paths
        /// for equality in the tests.
        /// </summary>
        private string NormalizePath(string path)
        {
            return path.Replace('\\','/');
        }

        /// <summary>
        /// Set up test specific paths
        /// </summary>
        /// <param name="testName">Name of the test</param>
        /// <param name="testPath">Optional path to the test project</param>
        private void SetUpForTest(string testName, string testPath = null)
        {
            if (testPath == null) {
                testPath = testName;
            }

            SetUpCommonPaths();

            testId = $"{testName}_run-{Guid.NewGuid().ToString()}";
            Console.WriteLine($"TestID for test: {testId}");

            // Paths for test data
            testProjectDir = NormalizePath(Path.Combine(testDataDir, testPath));
            testOutDir = NormalizePath(Path.Combine(testOutBaseDir, testId));
        }

        /// <summary>
        /// Run "dotnet build" on the test's project file.
        /// </summary>
        /// <param name="testName">Name of test and name of directory containing the test</param>
        /// <param name="filesToGenerate">Tell the fake protoc script which files to generate</param>
        /// <param name="testId">A unique ID for the test run - used to create results file</param>
        private void TryRunMsBuild(string testName, string filesToGenerate)
        {
            Directory.CreateDirectory(testOutDir);

            // create the arguments for the "dotnet build"
            var args = $"build -p:{TASKS_ASSEMBLY_PROPERTY}={tasksAssembly}"
                + $" -p:TestOutDir={testOutDir}"
                + $" -p:BaseOutputPath={testOutDir}/bin/"
                + $" -p:BaseIntermediateOutputPath={testOutDir}/obj/"
                + $" -p:{TOOLS_BUILD_DIR_PROPERTY}={grpcToolsBuildDir}"
                + $" -p:{PROTBUF_FULLPATH_PROPERTY}={fakeProtoc}"
                + $" -p:{PLUGIN_FULLPATH_PROPERTY}=dummy-plugin-not-used"
                + $" -fl -flp:LogFile={testOutDir}/log/msbuild.log;verbosity={MSBUILD_LOG_VERBOSITY}"
                + $" msbuildtest.csproj";

            // To pass additional parameters to fake protoc process
            // we need to use environment variables
            var envVariables = new StringDictionary {
                { "FAKEPROTOC_PROJECTDIR", testProjectDir },
                { "FAKEPROTOC_OUTDIR", testOutDir },
                { "FAKEPROTOC_GENERATE_EXPECTED", filesToGenerate },
                { "FAKEPROTOC_TESTID", testId }
            };

            // Run the "dotnet build"
            ProcessMsbuild(args, testProjectDir, envVariables);

            // Check the results JSON matches the expected JSON
            Results actualResults = Results.Read(testOutDir + "/log/results.json");
            Results expectedResults = Results.Read(testProjectDir + "/expected.json");
            CompareResults(expectedResults, actualResults);
        }

        /// <summary>
        /// Run the "dotnet build" command
        /// </summary>
        /// <param name="args">arguments to the dotnet command</param>
        /// <param name="workingDirectory">working directory</param>
        /// <param name="envVariables">environment variables to set</param>
        private void ProcessMsbuild(string args, string workingDirectory, StringDictionary envVariables)
        {
            using (var process = new Process())
            {
                process.StartInfo.FileName = "dotnet";
                process.StartInfo.Arguments = args;
                process.StartInfo.RedirectStandardOutput = true;
                process.StartInfo.RedirectStandardError = true;
                process.StartInfo.WorkingDirectory = workingDirectory;
                process.StartInfo.UseShellExecute = false;
                StringDictionary procEnv = process.StartInfo.EnvironmentVariables;
                foreach (DictionaryEntry entry in envVariables)
                {
                    if (!procEnv.ContainsKey((string)entry.Key))
                    {
                        procEnv.Add((string)entry.Key, (string)entry.Value);
                    }
                }

                process.OutputDataReceived += (sender, e) => {
                    if (e.Data != null)
                    {
                        Console.WriteLine(e.Data);
                    }
                };
                process.ErrorDataReceived += (sender, e) => {
                    if (e.Data != null)
                    {
                        Console.WriteLine(e.Data);
                    }
                };

                process.Start();

                process.BeginErrorReadLine();
                process.BeginOutputReadLine();

                process.WaitForExit();
                Assert.AreEqual(0, process.ExitCode, "The dotnet/msbuild subprocess invocation exited with non-zero exitcode.");
            }
        }

        /// <summary>
        /// Compare the JSON results to the expected results
        /// </summary>
        /// <param name="expected"></param>
        /// <param name="actual"></param>
        private void CompareResults(Results expected, Results actual)
        {
            // Check set of .proto files processed is the same
            var protofiles = expected.ProtoFiles;
            CollectionAssert.AreEquivalent(protofiles, actual.ProtoFiles, "Set of .proto files being processed must match.");

            // check protoc arguments
            foreach (string protofile in protofiles)
            {
                var expectedArgs = expected.GetArgumentNames(protofile);
                var actualArgs = actual.GetArgumentNames(protofile);
                CollectionAssert.AreEquivalent(expectedArgs, actualArgs, $"Set of protoc arguments used for {protofile} must match.");

                // Check the values.
                // Any value with:
                // - IGNORE: - will not be compared but must exist
                // - REGEX: - compare using a regular expression
                // - anything else is an exact match
                // Expected results can also have tokens that are replaced before comparing:
                // - ${TEST_OUT_DIR} - the test output directory
                foreach (string argname in expectedArgs)
                {
                    var expectedValues = expected.GetArgumentValues(protofile, argname);
                    var actualValues = actual.GetArgumentValues(protofile, argname);

                    Assert.AreEqual(expectedValues.Count, actualValues.Count,
                                 $"{protofile}: Wrong number of occurrences of argument '{argname}'");

                    // Since generally the order of arguments on the commandline is important,
                    // it is fair to compare arguments with expected values one by one.
                    // Most arguments are only used at most once by the msbuild integration anyway.
                    for (int i = 0; i < expectedValues.Count; i++)
                    {
                        var expectedValue = ReplaceTokens(expectedValues[i]);
                        var actualValue = actualValues[i];

                        if (expectedValue.StartsWith("IGNORE:"))
                            continue;

                        var regexPrefix = "REGEX:";
                        if (expectedValue.StartsWith(regexPrefix))
                        {
                            string pattern = expectedValue.Substring(regexPrefix.Length);
                            Assert.IsTrue(Regex.IsMatch(actualValue, pattern),
                                 $"{protofile}: Expected value '{expectedValue}' for argument '{argname}'. Actual value: '{actualValue}'");
                        }
                        else
                        {
                            Assert.AreEqual(expectedValue, actualValue, $"{protofile}: Wrong value for argument '{argname}'");
                        }
                    }
                }
            }
        }

        private string ReplaceTokens(string original)
        {
            return original
                .Replace("${TEST_OUT_DIR}", testOutDir);
        }

        /// <summary>
        /// Helper class for formatting the string specifying the list of proto files and
        /// the expected generated files for each proto file.
        /// </summary>
        public class ExpectedFilesBuilder
        {
            private readonly List<string> protoAndFiles = new List<string>();

            public ExpectedFilesBuilder Add(string protoFile, params string[] files)
            {
                protoAndFiles.Add(protoFile + ":" + string.Join(";", files));
                return this;
            }

            public override string ToString()
            {
                return string.Join("|", protoAndFiles.ToArray());
            }
        }

        /// <summary>
        /// Hold the JSON results
        /// </summary>
        public class Results
        {
            /// <summary>
            /// JSON "Metadata"
            /// </summary>
            public Dictionary<string, string> Metadata { get; set; }
            
            /// <summary>
            /// JSON "Files"
            /// </summary>
            public Dictionary<string, Dictionary<string, List<string>>> Files { get; set; }

            /// <summary>
            /// Read a JSON file
            /// </summary>
            /// <param name="filepath"></param>
            /// <returns></returns>
            public static Results Read(string filepath)
            {
                using (StreamReader file = File.OpenText(filepath))
                {
                    JsonSerializer serializer = new JsonSerializer();
                    Results results = (Results)serializer.Deserialize(file, typeof(Results));
                    return results;
                }
            }

            /// <summary>
            /// Get the proto file names from the JSON
            /// </summary>
            public SortedSet<string> ProtoFiles => new SortedSet<string>(Files.Keys);

            /// <summary>
            /// Get the protoc arguments for the associated proto file
            /// </summary>
            /// <param name="protofile"></param>
            /// <returns></returns>
            public SortedSet<string> GetArgumentNames(string protofile)
            {
                Dictionary<string, List<string>> args;
                if (Files.TryGetValue(protofile, out args))
                {
                    return new SortedSet<string>(args.Keys);
                }
                else
                {
                    return new SortedSet<string>();
                }
            }

            /// <summary>
            /// Get the values for the named argument for the proto file
            /// </summary>
            /// <param name="protofile">proto file</param>
            /// <param name="name">argument</param>
            /// <returns></returns>
            public List<string> GetArgumentValues(string protofile, string name)
            {
                Dictionary<string, List<string>> args;
                if (Files.TryGetValue(protofile, out args))
                {
                    List<string> values;
                    if (args.TryGetValue(name, out values))
                    {
                        return new List<string>(values);
                    }
                }
                return new List<string>();
            }
        }
    }


}
