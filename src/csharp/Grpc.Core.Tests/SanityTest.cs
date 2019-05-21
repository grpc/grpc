#region Copyright notice and license

// Copyright 2015 gRPC authors.
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
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using Newtonsoft.Json;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class SanityTest
    {
        // TODO: make sanity test work for CoreCLR as well
#if !NETCOREAPP1_1 && !NETCOREAPP2_1
        /// <summary>
        /// Because we depend on a native library, sometimes when things go wrong, the
        /// entire NUnit test process crashes. To be able to track down problems better,
        /// the NUnit tests are run by run_tests.py script in a separate process per test class.
        /// The list of tests to run is stored in src/csharp/tests.json.
        /// This test checks that the tests.json file is up to date by discovering all the
        /// existing NUnit tests in all test assemblies and comparing to contents of tests.json.
        /// </summary>
        [Test]
        public void TestsJsonUpToDate()
        {
            var discoveredTests = DiscoverAllTestClasses();
            var testsFromFile
                = JsonConvert.DeserializeObject<Dictionary<string, List<string>>>(ReadTestsJson());

            Assert.AreEqual(discoveredTests, testsFromFile);
        }

        /// <summary>
        /// Gets list of all test classes obtained by inspecting all the test assemblies.
        /// </summary>
        private Dictionary<string, List<string>> DiscoverAllTestClasses()
        {
            var assemblies = GetTestAssemblies();

            var testsByAssembly = new Dictionary<string, List<string>>();
            foreach (var assembly in assemblies)
            {
                var testClasses = new List<string>();
                foreach (var t in assembly.GetTypes())
                {
                    foreach (var m in t.GetMethods())
                    {
                        var testAttributes = m.GetCustomAttributes(typeof(NUnit.Framework.TestAttribute), true);
                        var testCaseAttributes = m.GetCustomAttributes(typeof(NUnit.Framework.TestCaseAttribute), true);
                        if (testAttributes.Length > 0 || testCaseAttributes.Length > 0)
                        {
                            testClasses.Add(t.FullName);
                            break;
                        }
                    }
                }
                testClasses.Sort();
                testsByAssembly.Add(assembly.GetName().Name, testClasses);
            }
            return testsByAssembly;
        }

        /// <summary>
        /// Reads contents of tests.json file.
        /// </summary>
        private string ReadTestsJson()
        {
            var assemblyDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            var testsJsonFile = Path.Combine(assemblyDir, "..", "..", "..", "..", "tests.json");
            return File.ReadAllText(testsJsonFile);
        }

        private List<Assembly> GetTestAssemblies()
        {
            var result = new List<Assembly>();
            var executingAssembly = Assembly.GetExecutingAssembly();

            result.Add(executingAssembly);

            var otherAssemblies = new[] {
                "Grpc.Examples.Tests",
                "Grpc.HealthCheck.Tests",
                "Grpc.IntegrationTesting",
                "Grpc.Reflection.Tests",
                "Grpc.Tools.Tests",
            };
            foreach (var assemblyName in otherAssemblies)
            {
                var location = executingAssembly.Location.Replace("Grpc.Core.Tests", assemblyName);
                result.Add(Assembly.LoadFrom(location));
            }
            return result;
        }
#endif
    }
}
