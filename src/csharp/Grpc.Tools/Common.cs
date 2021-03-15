#region Copyright notice and license

// Copyright 2018 gRPC authors.
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
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security;
using Grpc.Core.Internal;

namespace Grpc.Tools
{
    // Metadata names (MSBuild item attributes) that we refer to often.
    static class Metadata
    {
        // On output dependency lists.
        public static string Source = "Source";
        // On Protobuf items.
        public static string ProtoRoot = "ProtoRoot";
        public static string OutputDir = "OutputDir";
        public static string GrpcServices = "GrpcServices";
        public static string GrpcOutputDir = "GrpcOutputDir";
    };

    // A few flags used to control the behavior under various platforms.
    internal static class Platform
    {
        public enum OsKind { Unknown, Windows, Linux, MacOsX };
        public static readonly OsKind Os;

        public enum CpuKind { Unknown, X86, X64 };
        public static readonly CpuKind Cpu;

        // This is not necessarily true, but good enough. BCL lacks a per-FS
        // API to determine file case sensitivity.
        public static bool IsFsCaseInsensitive => Os == OsKind.Windows;
        public static bool IsWindows => Os == OsKind.Windows;

        static Platform()
        {
            switch (CommonPlatformDetection.GetOSKind())
            {
                case CommonPlatformDetection.OSKind.Windows: Os = OsKind.Windows; break;
                case CommonPlatformDetection.OSKind.Linux: Os = OsKind.Linux; break;
                case CommonPlatformDetection.OSKind.MacOSX: Os = OsKind.MacOsX; break;
                default: Os = OsKind.Unknown; break;
            }

            switch (CommonPlatformDetection.GetProcessArchitecture())
            {
                case CommonPlatformDetection.CpuArchitecture.X86: Cpu = CpuKind.X86; break;
                case CommonPlatformDetection.CpuArchitecture.X64: Cpu = CpuKind.X64; break;
                default: Cpu = CpuKind.Unknown; break;
            }
        }
    };

    // Exception handling helpers.
    static class Exceptions
    {
        // Returns true iff the exception indicates an error from an I/O call. See
        // https://github.com/Microsoft/msbuild/blob/v15.4.8.50001/src/Shared/ExceptionHandling.cs#L101
        static public bool IsIoRelated(Exception ex) =>
            ex is IOException ||
            (ex is ArgumentException && !(ex is ArgumentNullException)) ||
            ex is SecurityException ||
            ex is UnauthorizedAccessException ||
            ex is NotSupportedException;
    };

    // String helpers.
    static class Strings
    {
        // Compare string to argument using OrdinalIgnoreCase comparison.
        public static bool EqualNoCase(this string a, string b) =>
            string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
    }
}
