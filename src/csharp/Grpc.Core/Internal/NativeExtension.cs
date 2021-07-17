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
        // Enviroment variable can be used to force loading the native extension from given location.
        private const string CsharpExtOverrideLocationEnvVarName = "GRPC_CSHARP_EXT_OVERRIDE_LOCATION";
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

            // Initialize
            NativeCallbackDispatcher.Init(this.nativeMethods);

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
        /// Detects which configuration of native extension to load and explicitly loads the dynamic library.
        /// The explicit load makes sure that we can detect any loading problems early on.
        /// </summary>
        private static NativeMethods LoadNativeMethodsUsingExplicitLoad()
        {
            // NOTE: a side effect of searching the native extension's library file relatively to the assembly location is that when Grpc.Core assembly
            // is loaded via reflection from a different app's context, the native extension is still loaded correctly
            // (while if we used [DllImport], the native extension won't be on the other app's search path for shared libraries).
            var assemblyDirectory = GetAssemblyDirectory();

            // With "classic" VS projects, the native libraries get copied using a .targets rule to the build output folder
            // alongside the compiled assembly.
            // With dotnet SDK projects targeting net45 framework, the native libraries (just the required ones)
            // are similarly copied to the built output folder, through the magic of Microsoft.NETCore.Platforms.
            var classicPath = Path.Combine(assemblyDirectory, GetNativeLibraryFilename());

            // With dotnet SDK project targeting netcoreappX.Y, projects will use Grpc.Core assembly directly in the location where it got restored
            // by nuget. We locate the native libraries based on known structure of Grpc.Core nuget package.
            // When "dotnet publish" is used, the runtimes directory is copied next to the published assemblies.
            string runtimesDirectory = string.Format("runtimes/{0}/native", GetRuntimeIdString());
            var netCorePublishedAppStylePath = Path.Combine(assemblyDirectory, runtimesDirectory, GetNativeLibraryFilename());
            var netCoreAppStylePath = Path.Combine(assemblyDirectory, "../..", runtimesDirectory, GetNativeLibraryFilename());

            // Look for the native library in all possible locations in given order.
            string[] paths = new[] { classicPath, netCorePublishedAppStylePath, netCoreAppStylePath};

            // The UnmanagedLibrary mechanism for loading the native extension while avoiding
            // direct use of DllImport is quite complicated but it is currently needed to ensure:
            // 1.) the native extension is loaded eagerly (needed to avoid startup issues)
            // 2.) less common scenarios (such as loading Grpc.Core.dll by reflection) still work
            // 3.) loading native extension from an arbitrary location when set by an enviroment variable
            // TODO(jtattermusch): revisit the possibility of eliminating UnmanagedLibrary completely in the future.
            return new NativeMethods(new UnmanagedLibrary(paths));
        }

        /// <summary>
        /// Loads native methods using the <c>[DllImport(LIBRARY_NAME)]</c> attributes.
        /// Note that this way of loading the native extension is "lazy" and doesn't
        /// detect any "missing library" problems until we actually try to invoke the native methods
        /// (which could be too late and could cause weird hangs at startup)
        /// </summary>
        private static NativeMethods LoadNativeMethodsUsingDllImports()
        {
            // While in theory, we could just use [DllImport("grpc_csharp_ext")] for all the platforms
            // and operating systems, the native libraries in the nuget package
            // need to be laid out in a way that still allows things to work well under
            // the legacy .NET Framework (where native libraries are a concept unknown to the runtime).
            // Therefore, we use several flavors of the DllImport attribute
            // (e.g. the ".x86" vs ".x64" suffix) and we choose the one we want at runtime.
            // The classes with the list of DllImport'd methods are code generated,
            // so having more than just one doesn't really bother us.

            // on Windows, the DllImport("grpc_csharp_ext.x64") doesn't work
            // but DllImport("grpc_csharp_ext.x64.dll") does, so we need a special case for that.
            // See https://github.com/dotnet/coreclr/pull/17505 (fixed in .NET Core 3.1+)
            bool useDllSuffix = PlatformApis.IsWindows;
            if (PlatformApis.ProcessArchitecture == CommonPlatformDetection.CpuArchitecture.X64)
            {
                if (useDllSuffix)
                {
                    return new NativeMethods(new NativeMethods.DllImportsFromSharedLib_x64_dll());
                }
                return new NativeMethods(new NativeMethods.DllImportsFromSharedLib_x64());
            }
            else if (PlatformApis.ProcessArchitecture == CommonPlatformDetection.CpuArchitecture.X86)
            {
                if (useDllSuffix)
                {
                    return new NativeMethods(new NativeMethods.DllImportsFromSharedLib_x86_dll());
                }
                return new NativeMethods(new NativeMethods.DllImportsFromSharedLib_x86());
            }
            else if (PlatformApis.ProcessArchitecture == CommonPlatformDetection.CpuArchitecture.Arm64)
            {
                return new NativeMethods(new NativeMethods.DllImportsFromSharedLib_arm64());
            }
            else
            {
                throw new InvalidOperationException($"Unsupported architecture \"{PlatformApis.ProcessArchitecture}\".");
            }
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

            // Override location of grpc_csharp_ext native library with an environment variable
            // Use at your own risk! By doing this you take all the responsibility that the dynamic library
            // is of the correct version (needs to match the Grpc.Core assembly exactly) and of the correct platform/architecture.
            var nativeExtPathFromEnv = System.Environment.GetEnvironmentVariable(CsharpExtOverrideLocationEnvVarName);
            if (!string.IsNullOrEmpty(nativeExtPathFromEnv))
            {
                return new NativeMethods(new UnmanagedLibrary(new string[] { nativeExtPathFromEnv }));
            }

            if (IsNet5SingleFileApp())
            {
                // Ideally we'd want to always load the native extension explicitly
                // (to detect any potential problems early on and to avoid hard-to-debug startup issues)
                // but the mechanism we normally use doesn't work when running
                // as a single file app (see https://github.com/grpc/grpc/pull/24744).
                // Therefore in this case we simply rely
                // on the automatic [DllImport] loading logic to do the right thing.
                return LoadNativeMethodsUsingDllImports();
            }
            return LoadNativeMethodsUsingExplicitLoad();
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
            if (PlatformApis.IsUnityIOS)
            {
                return new NativeMethods(new NativeMethods.DllImportsFromStaticLib());
            }
            // most other platforms load unity plugins as a shared library
            return new NativeMethods(new NativeMethods.DllImportsFromSharedLib());
        }

        /// <summary>
        /// Return native method delegates when running on the Xamarin platform.
        /// On Xamarin, the standard <c>[DllImport]</c> loading logic just works
        /// as the native library metadata is provided by the <c>AndroidNativeLibrary</c> or
        /// <c>NativeReference</c> items in the Xamarin projects (injected automatically
        /// by the Grpc.Core.Xamarin nuget).
        /// WARNING: Xamarin support is experimental and work-in-progress. Don't expect it to work.
        /// </summary>
        private static NativeMethods LoadNativeMethodsXamarin()
        {
            if (PlatformApis.IsXamarinAndroid)
            {
                return new NativeMethods(new NativeMethods.DllImportsFromSharedLib());
            }
            return new NativeMethods(new NativeMethods.DllImportsFromStaticLib());
        }

        private static string GetAssemblyDirectory()
        {
            var assembly = typeof(NativeExtension).GetTypeInfo().Assembly;
#if NETSTANDARD
            // Assembly.EscapedCodeBase does not exist under CoreCLR, but assemblies imported from a nuget package
            // don't seem to be shadowed by DNX-based projects at all.
            var assemblyLocation = assembly.Location;
            if (string.IsNullOrEmpty(assemblyLocation))
            {
                // In .NET5 single-file deployments, assembly.Location won't be available
                // and we can use it for detecting whether we are running as a single file app.
                // Also see https://docs.microsoft.com/en-us/dotnet/core/deploying/single-file#other-considerations
                return null;
            }
            return Path.GetDirectoryName(assemblyLocation);
#else
            // If assembly is shadowed (e.g. in a webapp), EscapedCodeBase is pointing
            // to the original location of the assembly, and Location is pointing
            // to the shadow copy. We care about the original location because
            // the native dlls don't get shadowed.

            var escapedCodeBase = assembly.EscapedCodeBase;
            if (IsFileUri(escapedCodeBase))
            {
                return Path.GetDirectoryName(new Uri(escapedCodeBase).LocalPath);
            }
            return Path.GetDirectoryName(assembly.Location);
#endif
        }

        private static bool IsNet5SingleFileApp()
        {
            // Use a heuristic that GetAssemblyDirectory() will return null for single file apps.
            return PlatformApis.IsNet5OrHigher && GetAssemblyDirectory() == null;
        }

#if !NETSTANDARD
        private static bool IsFileUri(string uri)
        {
            return uri.ToLowerInvariant().StartsWith(Uri.UriSchemeFile);
        }
#endif

        private static string GetRuntimeIdString()
        {
            string architecture = GetArchitectureString();
            if (PlatformApis.IsWindows)
            {
                return string.Format("win-{0}", architecture);
            }
            if (PlatformApis.IsLinux)
            {
                return string.Format("linux-{0}", architecture);
            }
            if (PlatformApis.IsMacOSX)
            {
                return string.Format("osx-{0}", architecture);
            }
            throw new InvalidOperationException("Unsupported platform.");
        }

        private static string GetArchitectureString()
        {
            switch (PlatformApis.ProcessArchitecture)
            {
                case CommonPlatformDetection.CpuArchitecture.X86:
                  return "x86";
                case CommonPlatformDetection.CpuArchitecture.X64:
                  return "x64";
                case CommonPlatformDetection.CpuArchitecture.Arm64:
                  return "arm64";
                default:
                  throw new InvalidOperationException($"Unsupported architecture \"{PlatformApis.ProcessArchitecture}\".");
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
