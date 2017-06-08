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
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Utility methods for detecting platform and architecture.
    /// </summary>
    internal static class PlatformApis
    {
        static readonly bool isLinux;
        static readonly bool isMacOSX;
        static readonly bool isWindows;
        static readonly bool isMono;
        static readonly bool isNetCore;

        static PlatformApis()
        {
#if NETSTANDARD1_5
            isLinux = RuntimeInformation.IsOSPlatform(OSPlatform.Linux);
            isMacOSX = RuntimeInformation.IsOSPlatform(OSPlatform.OSX);
            isWindows = RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
            isNetCore = RuntimeInformation.FrameworkDescription.StartsWith(".NET Core");
#else
            var platform = Environment.OSVersion.Platform;

            // PlatformID.MacOSX is never returned, commonly used trick is to identify Mac is by using uname.
            isMacOSX = (platform == PlatformID.Unix && GetUname() == "Darwin");
            isLinux = (platform == PlatformID.Unix && !isMacOSX);
            isWindows = (platform == PlatformID.Win32NT || platform == PlatformID.Win32S || platform == PlatformID.Win32Windows);
            isNetCore = false;
#endif
            isMono = Type.GetType("Mono.Runtime") != null;
        }

        public static bool IsLinux
        {
            get { return isLinux; }
        }

        public static bool IsMacOSX
        {
            get { return isMacOSX; }
        }

        public static bool IsWindows
        {
            get { return isWindows; }
        }

        public static bool IsMono
        {
            get { return isMono; }
        }

        /// <summary>
        /// true if running on .NET Core (CoreCLR), false otherwise.
        /// </summary>
        public static bool IsNetCore
        {
            get { return isNetCore; }
        }

        public static bool Is64Bit
        {
            get { return IntPtr.Size == 8; }
        }

        [DllImport("libc")]
        static extern int uname(IntPtr buf);

        static string GetUname()
        {
            var buffer = Marshal.AllocHGlobal(8192);
            try
            {
                if (uname(buffer) == 0)
                {
                    return Marshal.PtrToStringAnsi(buffer);
                }
                return string.Empty;
            }
            catch
            {
                return string.Empty;
            }
            finally
            {
                if (buffer != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(buffer);
                }
            }
        }
    }
}
