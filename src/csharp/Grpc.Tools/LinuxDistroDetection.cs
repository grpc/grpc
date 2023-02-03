#region Copyright notice and license

// Copyright 2023 gRPC authors.
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
using Grpc.Core.Internal;

namespace Grpc.Tools
{
    /// <summary>
    /// Detect the ID of the Linux distribution.
    /// If running on a Linux system the we try and detect the name of the distribution
    /// by reading the file /etc/os-release if it exists. Most commonly used Linux
    /// distributions have adopted the use of this file.
    /// <para> As we are only really interested in detecting Alpine linux to provide different
    /// binaries, and Alpine linux does use this file, then this is all we need to do.</para>
    ///
    /// </summary>
    internal static class LinuxDistroDetection
    {
        /// <summary>
        /// Get the Linux distribution ID (e.g. alpine, ubuntu, etc) or an empty
        /// string if it cannot be determined or is not Linux.
        /// </summary>
        /// <returns></returns>
        public static string GetLinuxDistroId()
        {
            if (Platform.Os == CommonPlatformDetection.OSKind.Linux)
            {
                return ReadLinuxDistroId("/etc/os-release");
            }
            return string.Empty;
        }

        /// <summary>
        /// Read a file to get the Linux distribution ID.
        /// This method is public to allow for testing.
        /// </summary>
        /// <param name="path"></param>
        /// <returns></returns>
        public static string ReadLinuxDistroId(string path)
        {
            const string IdPrefix = "ID=";

            string id = string.Empty;
            if (File.Exists(path))
            {
                try
                {
                    string[] lines = System.IO.File.ReadAllLines(path);
                    foreach (string line in lines)
                    {
                        if (line.StartsWith(IdPrefix) && line.Length > IdPrefix.Length)
                        {
                            id = line.Substring(IdPrefix.Length).ToLowerInvariant();
                            break;
                        }
                    }
                }
                catch (Exception e)
                {
                    // log the error and continue
                    Console.Error.WriteLine($"ReadDistroId: Unable to read {path}: Exception: {e}");
                }
            }

            return id;
        }

    }
}
