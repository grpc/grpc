#region Copyright notice and license

// Copyright 2016 gRPC authors.
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
using System.Reflection;
using Grpc.Core;
using Grpc.Core.Logging;
using NUnit.Common;
using NUnitLite;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Provides entry point for NUnitLite
    /// </summary>
    public class NUnitMain
    {
        public static int Main(string[] args)
        {
            // Make logger immune to NUnit capturing stdout and stderr to workaround https://github.com/nunit/nunit/issues/1406.
            GrpcEnvironment.SetLogger(new ConsoleLogger());
            return new AutoRun(typeof(NUnitMain).GetTypeInfo().Assembly).Execute(args);
        }
    }
}
