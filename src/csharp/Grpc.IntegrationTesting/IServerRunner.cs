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
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Utils;
using NUnit.Framework;
using Grpc.Testing;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Abstract server runner.
    /// </summary>
    public interface IServerRunner
    {
        /// <summary>
        /// Port on which the server is listening.
        /// </summary>
        int BoundPort { get; }
        
        /// <summary>
        /// Gets server stats.
        /// </summary>
        /// <returns>The stats.</returns>
        ServerStats GetStats(bool reset);

        /// <summary>
        /// Asynchronously stops the server.
        /// </summary>
        /// <returns>Task that finishes when server has shutdown.</returns>
        Task StopAsync();
    }
        
}
