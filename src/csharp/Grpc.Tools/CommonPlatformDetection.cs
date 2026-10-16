#region Copyright notice and license

// Copyright 2021 The gRPC Authors
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
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// This source file is shared by both Grpc.Core and Grpc.Tools to avoid duplication
    /// of platform detection code.
    /// </summary>
    internal static class CommonPlatformDetection
    {
        public enum OSKind { Unknown, Windows, Linux, MacOSX };
        public enum CpuArchitecture { Unknown, X86, X64, Arm64 };

        public static OSKind GetOSKind()
        {
#if NETSTANDARD || NETCORE
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                return OSKind.Windows;
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {
                return OSKind.Linux;
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            {
                return OSKind.MacOSX;
            }
            else
            {
                return OSKind.Unknown;
            }
#else
            var platform = Environment.OSVersion.Platform;
            if (platform == PlatformID.Win32NT || platform == PlatformID.Win32S || platform == PlatformID.Win32Windows)
            {
                return OSKind.Windows;
            }
            else if (platform == PlatformID.Unix && GetUname() == "Darwin")
            {
                // PlatformID.MacOSX is never returned, commonly used trick is to identify Mac is by using uname.
                return OSKind.MacOSX;
            }
            else if (platform == PlatformID.Unix)
            {
                // on legacy .NET Framework, our detection options are limited, so we treat
                // all other unix systems as Linux.
                return OSKind.Linux;
            }
            else
            {
                return OSKind.Unknown;
            }
#endif
        }

        public static CpuArchitecture GetProcessArchitecture()
        {
#if NETSTANDARD || NETCORE  
            switch (RuntimeInformation.ProcessArchitecture)
            {
                case Architecture.X86:
                    return CpuArchitecture.X86;
                case Architecture.X64:
                    return CpuArchitecture.X64;
                case Architecture.Arm64:
                    return CpuArchitecture.Arm64;
                // We do not support other architectures,
                // so we simply return "unrecognized".
                default: 
                   return CpuArchitecture.Unknown;
            }
#else
            // on legacy .NET Framework, RuntimeInformation is not available
            // but our choice of supported architectures there
            // is also very limited, so we simply take our best guess.
            return Environment.Is64BitProcess ? CpuArchitecture.X64 : CpuArchitecture.X86;
#endif
        }

        [DllImport("libc")]
        static extern int uname(IntPtr buf);

        // This code is copied from Grpc.Core/PlatformApis.cs
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
