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

        // The tests create files in the TEMP directory.

        // Whether or not to delete files created in the tests. When developing a test
        // or diagnosing a problem it may be useful to disable deleting the files so
        // that the output can be examined.
        private readonly bool doCleanup = true;

        // We try and delete old test output directories that may not have been deleted because
        // the test terminated before the cleanup code ran. We only delete these directories
        // if they are older than the age specified here. This prevents deleting directories
        // still in use if tests are run an parallel.
        private const int CLEANUP_DIR_AGE_MINUTES = 15;

        private string testId;
        private string fakeProtoc;
        private string grpcToolsDir;
        private string grpcToolsBuildDir;
        private string tasksAssembly;
        private string testDataDir;
        private string testProjectDir;
        private string testOutBaseDir;
        private string testOutDir;
        private string tempDir;

        [OneTimeSetUp]
        public void InitOnce()
        {
            SetUpCommonPaths();
            if (doCleanup)
            {
                // Delete old test directories than may have been left around
                // by a previous run.
                CleanupOldResults(testOutBaseDir);
            }
        }

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
            testId = Guid.NewGuid().ToString();
            Console.WriteLine($"TestID for test: {testId}");
        }

        [TearDown]
        public void AfterTest()
        {
            if (doCleanup)
            {
                if (Directory.Exists(testOutDir))
                {
                    DeleteDirectoryWithRetry(testOutDir);
                }
            }
        }

        [Test]
        public void TestSingleProto()
        {
            SetUpSpecificPaths("TestSingleProto");

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("file.proto", "File.cs", "FileGrpc.cs");

            TryRunMsBuild("TestSingleProto", expectedFiles.ToString());
        }

        [Test]
        public void TestMultipleProtos()
        {
            SetUpSpecificPaths("TestMultipleProtos");

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("file.proto", "File.cs", "FileGrpc.cs")
                .Add("protos/another.proto", "Another.cs", "AnotherGrpc.cs")
                .Add("second.proto", "Second.cs", "SecondGrpc.cs");

            TryRunMsBuild("TestMultipleProtos", expectedFiles.ToString());
        }

        [Test]
        public void TestAtInPath()
        {
            SetUpSpecificPaths("TestAtInPath");

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("@protos/file.proto", "File.cs", "FileGrpc.cs");

            TryRunMsBuild("TestAtInPath", expectedFiles.ToString());
        }

        [Test]
        public void TestProtoOutsideProject()
        {
            SetUpSpecificPaths("TestProtoOutsideProject/project");

            // The "out" directory is outside the project folder in this test case
            var outDir = Path.GetFullPath(testOutDir + "/");
            Console.WriteLine($"out = {outDir}");
            CleanupOldResults(outDir);

            var expectedFiles = new ExpectedFilesBuilder();
            expectedFiles.Add("../api/greet.proto", "Greet.cs", "GreetGrpc.cs");

            TryRunMsBuild("TestProtoOutsideProject/project", expectedFiles.ToString());
        }

        /// <summary>
        /// Set up common paths for all the tests
        /// </summary>
        private void SetUpCommonPaths()
        {
            var assemblyDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            var parentDir = System.IO.Directory.GetParent(assemblyDir).FullName;

            testDataDir = Path.GetFullPath(parentDir + "/../../IntegrationTests/");

            // Path for fake proto.
            // On Windows we have to wrap the python script in a BAT script since we can only
            // pass one executable name without parameters to the MSBuild
            // - e.g. we can't give "python fakeprotoc.py"
            var fakeProtocScript = Platform.IsWindows ? "fakeprotoc.bat" : "fakeprotoc.py";
            fakeProtoc = Path.GetFullPath($"{parentDir}/../../scripts/{fakeProtocScript}");

            // Paths for Grpc.Tools files
            grpcToolsDir = Path.GetFullPath($"{parentDir}/../../../Grpc.Tools");
            grpcToolsBuildDir = Path.GetFullPath($"{grpcToolsDir}/build");

            // Task assembly is needed to run the extension tasks.
            // In development environments you can use this one:
            //tasksAssembly = Path.GetFullPath($"{grpcToolsDir}/bin/Debug/netstandard1.3/{TASKS_ASSEMBLY_DLL}");
            // This one is provided for CI environments.
            tasksAssembly = Path.GetFullPath($"{testDataDir}/TasksAssembly/netstandard1.3/{TASKS_ASSEMBLY_DLL}");

            // output directory
            tempDir = Path.GetFullPath(System.IO.Path.GetTempPath()).Replace('\\','/');
            if (!tempDir.EndsWith("/"))
            {
                tempDir += "/";
            }
            testOutBaseDir = Path.GetFullPath(tempDir + "grpctoolstest/");
        }

        /// <summary>
        /// Set up test specific paths
        /// </summary>
        /// <param name="testName">Name of the test</param>
        private void SetUpSpecificPaths(string testName)
        {
            // Paths for test data
            testProjectDir = Path.GetFullPath(testDataDir + testName);
            testOutDir = Path.GetFullPath(testOutBaseDir + testId);
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
            // TODO improve this checking as we are comparing two sets - should check actual results
            // are a subset of expected results. At the moment there is only one value
            // in each set, but in theory there can be more than one value for repeated arguments.
            foreach (string protofile in protofiles)
            {
                SortedSet<string> expectedArgs = expected.GetArgumentNames(protofile);
                SortedSet<string> actualArgs = actual.GetArgumentNames(protofile);
                CollectionAssert.IsSupersetOf(actualArgs, expectedArgs, "Missing protoc arguments for " + protofile);

                // Check the values.
                // Any value with:
                // - IGNORE: - will not be compared but must exist
                // - REGEX: - compare using a regular expression
                // - anything else is an exact match
                // Expected results can also have tokens that are replace before comparing:
                // - ${TESTID} - the testID
                // - ${TEMP} - the path of te temporary directory
                foreach (string argname in expectedArgs)
                {
                    SortedSet<string> expectedValues = expected.GetArgumentValues(protofile, argname);
                    SortedSet<string> actualValuesSet = actual.GetArgumentValues(protofile, argname);

                    // Copy to array so we can index the array when printing out errors
                    string[] actualValues = new string[actualValuesSet.Count];
                    actualValuesSet.CopyTo(actualValues);

                    foreach (string value in expectedValues)
                    {
                        if (value.StartsWith("IGNORE:"))
                            continue;

                        string val = ReplaceTokens(value);
                        if (val.StartsWith("REGEX:"))
                        {
                            string pattern = val.Substring(6);
                            bool anyMatched = false;
                            foreach (string s in actualValues)
                            {
                                if (Regex.IsMatch(s, pattern))
                                {
                                    anyMatched = true;
                                    break;
                                }
                            }
                            Assert.IsTrue(anyMatched,
                                 $"Missing value for '{protofile}' with argument '{argname}' expected: '{val}'\n" +
                                 $"  actual: '{actualValues[0]}'");
                        }
                        else
                        {
                            Assert.IsTrue(actualValuesSet.Contains(val),
                                $"Missing value for '{protofile}' with argument '{argname}' expected: '{val}'\n" +
                                $"  actual: '{actualValues[0]}'");
                        }
                    }
                }
            }
        }

        private string ReplaceTokens(string original)
        {
            return original
                .Replace("${TESTID}", testId)
                .Replace("${TEMP}", tempDir);
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
        /// Get directories that match a UUID
        /// </summary>
        /// <param name="path"></param>
        /// <returns></returns>
        private  string[] SafeGetTestDirectories(string path)
        {
            try
            {
                // Matching directory names against a possible UUID format rather
                // than getting all directories - this is to be safe so we don't
                // accidently do a "rm *" if there is a coding error in the tests.
                return Directory.GetDirectories(path, "*-*-*-*-*");
            }
            catch(Exception e)
            {
                Console.WriteLine($"Unable to get test directories: {e}");
                return new string[0];
            }
        }

        /// <summary>
        /// Delete old directories under the given directory.
        /// The directory names must look like a UUID and must be older
        /// that CLEANUP_DIR_AGE_MINUTES minutes.
        /// </summary>
        /// <param name="baseDir"></param>
        private void CleanupOldResults(string baseDir)
        {
            if (Directory.Exists(baseDir))
            {
                DateTime newestTime = DateTime.Now.AddMinutes(-CLEANUP_DIR_AGE_MINUTES);

                string[] dirs = SafeGetTestDirectories(baseDir);
                foreach (string dir in dirs)
                {
                    try
                    {
                        DateTime creationTime = Directory.GetCreationTime(dir);
                        if (creationTime < newestTime)
                        {
                            DeleteDirectoryWithRetry(dir);
                        }
                    }
                    catch (Exception e)
                    {
                        Console.WriteLine($"Unable to delete test directory: {dir}: {e}");
                    }
                }
            }
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
