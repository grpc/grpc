#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
            string discoveredTestsJson = JsonConvert.SerializeObject(discoveredTests, Formatting.Indented);

            Assert.AreEqual(discoveredTestsJson, ReadTestsJson());
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
                        var attributes = m.GetCustomAttributes(typeof(NUnit.Framework.TestAttribute), true);
                        if (attributes.Length > 0)
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
            var testsJsonFile = Path.Combine(assemblyDir, "..", "..", "..", "tests.json");
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
                "Grpc.IntegrationTesting"
            };
            foreach (var assemblyName in otherAssemblies)
            {
                var location = executingAssembly.Location.Replace("Grpc.Core.Tests", assemblyName);
                result.Add(Assembly.LoadFrom(location));
            }
            return result;
        }
    }
}
