#region Copyright notice and license

// Copyright 2020 The gRPC Authors
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

namespace Grpc.Tools.Tests
{
    public class MsBuildEndToEndTest
    {
        private const string TASKS_ASSEMBLY_PROPERTY = "_Protobuf_MsBuildAssembly";
        private const string TASKS_ASSEMBLY_DLL = "Protobuf.MSBuild.dll";
        private const string PROTBUF_FULLPATH_PROPERTY = "Protobuf_ProtocFullPath";
        private const string TOOLS_BUILD_DIR_PROPERTY = "GrpcToolsBuildDir";

        [Test]
        public void MsBuildTestSingleProto()
        {
            TryRunMsBuild("testSingleProto",
                "file.proto:File.cs;FileGrpc.cs"
                );
        }

        [Test]
        public void MsBuildTestMultipleProtos()
        {
            TryRunMsBuild("testMultipleProtos",
                "file.proto:File.cs;FileGrpc.cs" +
                "|protos/another.proto:Another.cs;AnotherGrpc.cs"
                );
        }

        [Test]
        public void MsBuildTestAtInPath()
        {
            TryRunMsBuild("testAtInPath",
                "@protos/file.proto:File.cs;FileGrpc.cs"
                );
        }

        public void TryRunMsBuild(string testName, string filesToGenerate)
        {
            var assemblyDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            var parentDir = System.IO.Directory.GetParent(assemblyDir).FullName;

            // Path for mock protoc
            var mockProtoc = Path.GetFullPath($"{parentDir}/../../scripts/mockprotoc.bat");

            // Paths for Grpc.Tools files
            var grpcToolsDir = Path.GetFullPath($"{parentDir}/../../../Grpc.Tools");
            var grpcToolsBuildDir = Path.GetFullPath($"{grpcToolsDir}/build");
            var tasksAssembly = Path.GetFullPath($"{grpcToolsDir}/bin/Debug/netstandard1.3/{TASKS_ASSEMBLY_DLL}");

            // Paths for test data
            var testdataDir = Path.GetFullPath(parentDir+ "/../../../Grpc.Tools.TestData/");
            var testDir = Path.GetFullPath(testdataDir + testName);

            // Console.Error.WriteLine("Assembly location " + assemblyDir);
            // Console.Error.WriteLine("Working directory " + parentDir);
            // Console.Error.WriteLine("Testdata directory " + testdataDir);
            // Console.Error.WriteLine("Tasks assembly " + tasksAssembly);
            // Console.Error.WriteLine("Test directory " + testDir);

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

            //Assert.IsTrue(File.Exists(tasksAssembly), $"Cannot find assemly: {tasksAssembly}");

            var args = $"build -p:{TASKS_ASSEMBLY_PROPERTY}={tasksAssembly}"
                + $" -p:{TOOLS_BUILD_DIR_PROPERTY}={grpcToolsBuildDir}"
                + $" -p:{PROTBUF_FULLPATH_PROPERTY}={mockProtoc}"
                + $" -fl -flp:LogFile=log/msbuild.log;verbosity=diagnostic"
                + $" msbuildtest.csproj";

            //Console.Error.WriteLine("args: " + args);

            // Note - to pass additional parameters to mock protoc process
            // we need to use environment variables
            // MOCKPROTOC_PROJECTDIR
            // MOCKPROTOC_GENERATE_EXPECTED
            //
            StringDictionary envVariables = new StringDictionary();
            envVariables.Add("MOCKPROTOC_PROJECTDIR", testDir);
            envVariables.Add("MOCKPROTOC_GENERATE_EXPECTED", filesToGenerate);

            ProcessMsbuild(args, testDir, envVariables);

            // TODO check outputs

        }

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

        public static void DeleteDirectoryWithRetry(string path)
        {
            try
            {
                Directory.Delete(path, true);
            }
            catch (IOException)
            {
                System.Threading.Thread.Sleep(100);
                Directory.Delete(path, true);
            }
            catch (UnauthorizedAccessException)
            {
                System.Threading.Thread.Sleep(100);
                Directory.Delete(path, true);
            }
        }

    }


}
