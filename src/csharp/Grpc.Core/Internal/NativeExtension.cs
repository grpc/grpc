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
using System.IO;
using System.Reflection;

using Grpc.Core.Logging;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Takes care of loading C# native extension and provides access to PInvoke calls the library exports.
    /// </summary>
    internal sealed class NativeExtension
    {
        const string NativeLibrariesDir = "nativelibs";
        const string DnxStyleNativeLibrariesDir = "../../runtimes";
        const string NativeSubdir = "native";

        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<NativeExtension>();
        static readonly object staticLock = new object();
        static volatile NativeExtension instance;

        readonly NativeMethods nativeMethods;

        private NativeExtension()
        {
            this.nativeMethods = new NativeMethods(Load());
            
            // Redirect the the native logs as the very first thing after loading the native extension
            // to make sure we don't lose any logs.
            NativeLogRedirector.Redirect(this.nativeMethods);

            DefaultSslRootsOverride.Override(this.nativeMethods);

            Logger.Debug("gRPC native library loaded successfully.");
        }

        /// <summary>
        /// Gets singleton instance of this class.
        /// The native extension is loaded when called for the first time.
        /// </summary>
        public static NativeExtension Get()
        {
            if (instance == null)
            {
                lock (staticLock)
                {
                    if (instance == null) {
                        instance = new NativeExtension();
                    }
                }
            }
            return instance;
        }

        /// <summary>
        /// Provides access to the exported native methods.
        /// </summary>
        public NativeMethods NativeMethods
        {
            get { return this.nativeMethods; }
        }

        /// <summary>
        /// Detects which configuration of native extension to load and load it.
        /// </summary>
        private static UnmanagedLibrary Load()
        {
            // TODO: allow customizing path to native extension (possibly through exposing a GrpcEnvironment property).

            var assemblyDirectory = Path.GetDirectoryName(GetAssemblyPath());

            // With old-style VS projects, the native libraries get copied using a .targets rule to the build output folder
            // alongside the compiled assembly.
            var classicPath = Path.Combine(
                assemblyDirectory,
                NativeLibrariesDir,
                GetNugetPlatformString(),
                NativeSubdir,
                GetNativeLibraryFilename(true));

            // DNX-style project.json projects will use Grpc.Core assembly directly in the location where it got restored
            // by nuget. We locate the native libraries based on known structure of Grpc.Core nuget package.
            // This style is also used for dotnet CLI projects that target netstandard1.X frameworks.
            var dnxStylePath = Path.Combine(
                assemblyDirectory,
                DnxStyleNativeLibrariesDir,
                GetNugetPlatformString(),
                NativeSubdir,
                GetNativeLibraryFilename(true));

            // Newer versions of nuget can copy native libraries from /runtimes directory of the nuget package 
            // automatically (with help of Microsoft.NETCore.Platforms package).
            // This is needed for dotnet CLI projects targeting net45, because the .targets rules are ignored for such projects.
            var runtimesStylePath = Path.Combine(assemblyDirectory, GetNativeLibraryFilename(true));

            return new UnmanagedLibrary(new string[] {classicPath, dnxStylePath, runtimesStylePath});
        }

        private static string GetAssemblyPath()
        {
            var assembly = typeof(NativeExtension).GetTypeInfo().Assembly;
#if NETSTANDARD1_5
            // Assembly.EscapedCodeBase does not exist under CoreCLR, but assemblies imported from a nuget package
            // don't seem to be shadowed by DNX-based projects at all.
            return assembly.Location;
#else
            // If assembly is shadowed (e.g. in a webapp), EscapedCodeBase is pointing
            // to the original location of the assembly, and Location is pointing
            // to the shadow copy. We care about the original location because
            // the native dlls don't get shadowed.

            var escapedCodeBase = assembly.EscapedCodeBase;
            if (IsFileUri(escapedCodeBase))
            {
                return new Uri(escapedCodeBase).LocalPath;
            }
            return assembly.Location;
#endif
        }

#if !NETSTANDARD1_5
        private static bool IsFileUri(string uri)
        {
            return uri.ToLowerInvariant().StartsWith(Uri.UriSchemeFile);
        }
#endif

        /// <summary>
        /// Platform string understood by nuget under /runtimes.
        /// </summary>
        /// <returns>The nuget platform string.</returns>
        private static string GetNugetPlatformString()
        {
            if (PlatformApis.IsWindows)
            {
                return "win";
            }
            if (PlatformApis.IsLinux)
            {
                return "linux";
            }
            if (PlatformApis.IsMacOSX)
            {
                return "osx";
            }
            throw new InvalidOperationException("Unsupported platform.");
        }

        // Currently, only Intel platform is supported.
        private static string GetArchitectureString()
        {
            if (PlatformApis.Is64Bit)
            {
                return "x64";
            }
            else
            {
                return "x86";
            }
        }

        // platform specific file name of the extension library
        private static string GetNativeLibraryFilename(bool withArchitecture)
        {
            string archSuffix = withArchitecture ? "." + GetArchitectureString() : "";
            if (PlatformApis.IsWindows)
            {
                return "grpc_csharp_ext" + archSuffix + ".dll";
            }
            if (PlatformApis.IsLinux)
            {
                return "libgrpc_csharp_ext" + archSuffix + ".so";
            }
            if (PlatformApis.IsMacOSX)
            {
                return "libgrpc_csharp_ext" + archSuffix + ".dylib";
            }
            throw new InvalidOperationException("Unsupported platform.");
        }
    }
}
