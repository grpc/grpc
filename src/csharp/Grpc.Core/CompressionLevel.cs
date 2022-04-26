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

namespace Grpc.Core
{
    /// <summary>
    /// Compression level based on grpc_compression_level from grpc/compression.h
    /// </summary>
    public enum CompressionLevel
    {
        /// <summary>
        /// No compression.
        /// </summary>
        None = 0,

        /// <summary>
        /// Low compression.
        /// </summary>
        Low,

        /// <summary>
        /// Medium compression.
        /// </summary>
        Medium,

        /// <summary>
        /// High compression.
        /// </summary>
        High,
    }
}
