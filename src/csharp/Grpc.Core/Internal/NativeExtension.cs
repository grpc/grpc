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
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<NativeExtension>();
        static readonly object staticLock = new object();
        static volatile NativeExtension instance;

        readonly NativeMethods nativeMethods;

        private NativeExtension()
        {
            this.nativeMethods = LoadNativeMethods();
            
            // Redirect the native logs as the very first thing after loading the native extension
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
        private static UnmanagedLibrary LoadUnmanagedLibrary()
        {
            // TODO: allow customizing path to native extension (possibly through exposing a GrpcEnvironment property).
            // See https://github.com/grpc/grpc/pull/7303 for one option.
            var assemblyDirectory = Path.GetDirectoryName(GetAssemblyPath());

            // With "classic" VS projects, the native libraries get copied using a .targets rule to the build output folder
            // alongside the compiled assembly.
            // With dotnet cli projects targeting net45 framework, the native libraries (just the required ones)
            // are similarly copied to the built output folder, through the magic of Microsoft.NETCore.Platforms.
            var classicPath = Path.Combine(assemblyDirectory, GetNativeLibraryFilename());

            // With dotnet cli project targeting netcoreappX.Y, projects will use Grpc.Core assembly directly in the location where it got restored
            // by nuget. We locate the native libraries based on known structure of Grpc.Core nuget package.
            // When "dotnet publish" is used, the runtimes directory is copied next to the published assemblies.
            string runtimesDirectory = string.Format("runtimes/{0}/native", GetPlatformString());
            var netCorePublishedAppStylePath = Path.Combine(assemblyDirectory, runtimesDirectory, GetNativeLibraryFilename());
            var netCoreAppStylePath = Path.Combine(assemblyDirectory, "../..", runtimesDirectory, GetNativeLibraryFilename());

            // Look for the native library in all possible locations in given order.
            string[] paths = new[] { classicPath, netCorePublishedAppStylePath, netCoreAppStylePath};
            return new UnmanagedLibrary(paths);
        }

        /// <summary>
        /// Loads native extension and return native methods delegates.
        /// </summary>
        private static NativeMethods LoadNativeMethods()
        {
            if (PlatformApis.IsUnity)
            {
                return LoadNativeMethodsUnity();
            }
            if (PlatformApis.IsXamarin)
            {
                return LoadNativeMethodsXamarin();
            }
            return new NativeMethods(LoadUnmanagedLibrary());
        }

        /// <summary>
        /// Return native method delegates when running on Unity platform.
        /// Unity does not use standard NuGet packages and the native library is treated
        /// there as a "native plugin" which is (provided it has the right metadata)
        /// automatically made available to <c>[DllImport]</c> loading logic.
        /// WARNING: Unity support is experimental and work-in-progress. Don't expect it to work.
        /// </summary>
        private static NativeMethods LoadNativeMethodsUnity()
        {
            switch (PlatformApis.GetUnityRuntimePlatform())
            {
                case "IPhonePlayer":
                    return new NativeMethods(new NativeMethods.DllImportsFromStaticLib());
                default:
                    // most other platforms load unity plugins as a shared library
                    return new NativeMethods(new NativeMethods.DllImportsFromSharedLib());
            }
        }

        /// <summary>
        /// Return native method delegates when running on the Xamarin platform.
        /// WARNING: Xamarin support is experimental and work-in-progress. Don't expect it to work.
        /// </summary>
        private static NativeMethods LoadNativeMethodsXamarin()
        {
            if (PlatformApis.IsXamarinAndroid)
            {
                return new NativeMethods(new NativeMethods.DllImportsFromSharedLib());
            }
            // not tested yet
            return new NativeMethods(new NativeMethods.DllImportsFromStaticLib());
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

        private static string GetPlatformString()
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
        private static string GetNativeLibraryFilename()
        {
            string architecture = GetArchitectureString();
            if (PlatformApis.IsWindows)
            {
                return string.Format("grpc_csharp_ext.{0}.dll", architecture);
            }
            if (PlatformApis.IsLinux)
            {
                return string.Format("libgrpc_csharp_ext.{0}.so", architecture);
            }
            if (PlatformApis.IsMacOSX)
            {
                return string.Format("libgrpc_csharp_ext.{0}.dylib", architecture);
            }
            throw new InvalidOperationException("Unsupported platform.");
        }
    }
}
