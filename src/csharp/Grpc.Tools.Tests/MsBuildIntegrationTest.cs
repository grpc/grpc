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
    public class MsBuildIntegrationTest
    {
        private const string TASKS_ASSEMBLY_PROPERTY = "_Protobuf_MsBuildAssembly";
        private const string TASKS_ASSEMBLY_DLL = "Protobuf.MSBuild.dll";
        private const string PROTBUF_FULLPATH_PROPERTY = "Protobuf_ProtocFullPath";
        private const string PLUGIN_FULLPATH_PROPERTY = "gRPC_PluginFullPath";
        private const string TOOLS_BUILD_DIR_PROPERTY = "GrpcToolsBuildDir";

        private static bool isMono = Type.GetType("Mono.Runtime") != null;

        private void SkipIfMonoOrNet45()
        {
            // We only want to run these tests once. This test class is just a driver
            // for calling the "dotnet build" processes, so it doesn't matter what
            // the runtime of this class actually is.
            //
            // If we were to allow the tests to be run on both .NET Framework (or Mono) and
            // .NET Core then we could get into a situation where both are running in
            // parallel which would cause the tests to fail as both would be writing to
            // the same files.

            if (isMono)
            {
                Assert.Ignore("Skipping test when mono runtime");
            }
#if NET45
            Assert.Ignore("Skipping test when NET45");
#endif
        }

        [Test]
        public void TestSingleProto()
        {
            SkipIfMonoOrNet45();
            string testId = Guid.NewGuid().ToString();

            TryRunMsBuild("TestSingleProto",
                "file.proto:File.cs;FileGrpc.cs",
                testId
                );
        }

        [Test]
        public void TestMultipleProtos()
        {
            SkipIfMonoOrNet45();
            string testId = Guid.NewGuid().ToString();

            TryRunMsBuild("TestMultipleProtos",
                "file.proto:File.cs;FileGrpc.cs" +
                "|protos/another.proto:Another.cs;AnotherGrpc.cs" +
                "|second.proto:Second.cs;SecondGrpc.cs",
                testId
                );
        }

        [Test]
        public void TestAtInPath()
        {
            SkipIfMonoOrNet45();
            string testId = Guid.NewGuid().ToString();

            TryRunMsBuild("TestAtInPath",
                "@protos/file.proto:File.cs;FileGrpc.cs",
                testId
                );
        }

        [Test]
        public void TestProtoOutsideProject()
        {
            SkipIfMonoOrNet45();
            string testId = Guid.NewGuid().ToString();

            TryRunMsBuild("TestProtoOutsideProject/project",
                "../api/greet.proto:Greet.cs;GreetGrpc.cs",
                testId
                );
        }

        /// <summary>
        /// Run "dotnet build" on the test's project file.
        /// </summary>
        /// <param name="testName">Name of test and name of directory containing the test</param>
        /// <param name="filesToGenerate">Tell the fake protoc script which files to generate</param>
        /// <param name="testId">A unique ID for the test run - used to create results file</param>
        private void TryRunMsBuild(string testName, string filesToGenerate, string testId)
        {
            var assemblyDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            var parentDir = System.IO.Directory.GetParent(assemblyDir).FullName;

            // Path for fake proto
            var fakeProtocScript = Platform.IsWindows ? "fakeprotoc.bat" : "fakeprotoc.py";
            var fakeProtoc = Path.GetFullPath($"{parentDir}/../../scripts/{fakeProtocScript}");

            // Paths for Grpc.Tools files
            var grpcToolsDir = Path.GetFullPath($"{parentDir}/../../../Grpc.Tools");
            var grpcToolsBuildDir = Path.GetFullPath($"{grpcToolsDir}/build");
            // Task assembly is needed to run the extension tasks
            var tasksAssembly = Path.GetFullPath($"{grpcToolsDir}/bin/Debug/netstandard1.3/{TASKS_ASSEMBLY_DLL}");

            // Paths for test data
            var testdataDir = Path.GetFullPath(parentDir+ "/../../Integration.Tests/");
            var testDir = Path.GetFullPath(testdataDir + testName);

            Console.WriteLine($"testDir = {testDir}");

            // Clean up test output dirs: bin obj log
            if (Directory.Exists(testDir+"/bin"))
            {
                DeleteDirectoryWithRetry(testDir + "/bin");
            }

            if (Directory.Exists(testDir + "/obj"))
            {
                DeleteDirectoryWithRetry(testDir + "/obj");
            }

            if (Directory.Exists(testDir + "/log"))
            {
                DeleteDirectoryWithRetry(testDir + "/log");
            }

            _ = Directory.CreateDirectory(testDir + "/log");

            // create the arguments for the "dotnet build"
            var args = $"build -p:{TASKS_ASSEMBLY_PROPERTY}={tasksAssembly}"
                + $" -p:{TOOLS_BUILD_DIR_PROPERTY}={grpcToolsBuildDir}"
                + $" -p:{PROTBUF_FULLPATH_PROPERTY}={fakeProtoc}"
                + $" -p:{PLUGIN_FULLPATH_PROPERTY}=dummy-plugin-not-used"
                + $" -fl -flp:LogFile=log/msbuild.log;verbosity=detailed"
                + $" msbuildtest.csproj";

            // To pass additional parameters to fake protoc process
            // we need to use environment variables
            var envVariables = new StringDictionary {
                { "FAKEPROTOC_PROJECTDIR", testDir },
                { "FAKEPROTOC_GENERATE_EXPECTED", filesToGenerate },
                { "FAKEPROTOC_TESTID", testId }
            };

            // Run the "dotnet build"
            ProcessMsbuild(args, testDir, envVariables);

            // Check the results JSON matches the expected JSON
            Results actualResults = Results.Read(testDir + "/log/" + testId + ".json");
            Results expectedResults = Results.Read(testDir + "/expected.json");
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
                Assert.AreEqual(0, process.ExitCode);
            }
        }

        /// <summary>
        /// Compare the JSON results to the expected results
        /// </summary>
        /// <param name="expected"></param>
        /// <param name="actual"></param>
        private void CompareResults(Results expected, Results actual)
        {
            // Check proto files
            SortedSet<string> protofiles = expected.ProtoFiles;
            CollectionAssert.AreEqual(protofiles, actual.ProtoFiles, "Proto files do not match");

            // check protoc arguments
            foreach (string protofile in protofiles)
            {
                SortedSet<string> expectedArgs = expected.GetArgumentNames(protofile);
                SortedSet<string> actualArgs = actual.GetArgumentNames(protofile);
                CollectionAssert.IsSupersetOf(actualArgs, expectedArgs, "Missing protoc arguments for " + protofile);

                // check the values
                // Any argument with value starting IGNORE: will not be compared but must exist
                foreach (string argname in expectedArgs)
                {
                    SortedSet<string> expectedValues = expected.GetArgumentValues(protofile, argname);
                    SortedSet<string> actualValues = actual.GetArgumentValues(protofile, argname);
                    foreach (string value in expectedValues)
                    {
                        if (value.StartsWith("IGNORE:"))
                            continue;
                        Assert.IsTrue(actualValues.Contains(value),
                            $"Missing value for '{protofile}' with argument '{argname}' expected: '{value}'");
                    }
                }
            }
        }

        /// <summary>
        /// Delete directory with retry
        /// </summary>
        /// <remarks>
        /// Sometimes on Windows if the file explorer is open then a directory
        /// may not get completely deleted on one try.
        /// </remarks>
        /// <param name="path"></param>
        public static void DeleteDirectoryWithRetry(string path)
        {
            try
            {
                Directory.Delete(path, true);
            }
            catch (IOException)
            {
                System.Threading.Thread.Sleep(200);
                Directory.Delete(path, true);
            }
            catch (UnauthorizedAccessException)
            {
                System.Threading.Thread.Sleep(200);
                Directory.Delete(path, true);
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
            public SortedSet<string> GetArgumentValues(string protofile, string name)
            {
                Dictionary<string, List<string>> args;
                if (Files.TryGetValue(protofile, out args))
                {
                    List<string> values;
                    if (args.TryGetValue(name, out values))
                    {
                        return new SortedSet<string>(values);
                    }
                }
                return new SortedSet<string>();
            }
        }
    }


}
