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
        /// Detects which configuration of native extension to load and load it.
        /// </summary>
        private static NativeMethods LoadNativeMethodsLegacyNetFramework()
        {
            // TODO: allow customizing path to native extension (possibly through exposing a GrpcEnvironment property).
            // See https://github.com/grpc/grpc/pull/7303 for one option.
            var assemblyDirectory = GetAssemblyDirectory();

            // With "classic" VS projects, the native libraries get copied using a .targets rule to the build output folder
            // alongside the compiled assembly.
            var classicPath = Path.Combine(assemblyDirectory, GetNativeLibraryFilename());

            // Look for the native library in all possible locations in given order.
            string[] paths = new[] { classicPath };

            // TODO(jtattermusch): the UnmanagedLibrary mechanism for loading the native extension while avoiding
            // direct use of DllImport is quite complicated and is currently only needed to cover some niche scenarios
            // (such legacy .NET Framework projects that use assembly shadowing) - everything else can be covered
            // by using the [DllImport]. We should investigate the possibility of eliminating UnmanagedLibrary completely
            // in the future.
            return new NativeMethods(new UnmanagedLibrary(paths));
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
            if (PlatformApis.IsNetCore)
            {
                // On .NET Core, native libraries are a supported feature and the SDK makes
                // sure that the native library is made available in the right location and that
                // they will be discoverable by the [DllImport] default loading mechanism,
                // even in some of the more exotic situations such as single file apps.
                //
                // While in theory, we could just [DllImport("grpc_csharp_ext")] for all the platforms
                // and operating systems, the native libraries in the nuget package
                // need to be laid out in a way that still allows things to work well under
                // the legacy .NET Framework (where native libraries are a concept unknown to the runtime).
                // Therefore, we use several flavors of the DllImport attribute
                // (e.g. the ".x86" vs ".x64" suffix) and we choose the one we want at runtime.
                // The classes with the list of DllImport'd methods are code generated,
                // so having more than just one doesn't really bother us.

                // on Windows, the DllImport("grpc_csharp_ext.x64") doesn't work for some reason,
                // but DllImport("grpc_csharp_ext.x64.dll") does, so we need a special case for that.
                bool useDllSuffix = PlatformApis.IsWindows;
                if (PlatformApis.Is64Bit)
                {
                    if (useDllSuffix)
                    {
                        return new NativeMethods(new NativeMethods.DllImportsFromSharedLib_x64_dll());
                    }
                    return new NativeMethods(new NativeMethods.DllImportsFromSharedLib_x64());
                }
                else
                {
                    if (useDllSuffix)
                    {
                        return new NativeMethods(new NativeMethods.DllImportsFromSharedLib_x86_dll());
                    }
                    return new NativeMethods(new NativeMethods.DllImportsFromSharedLib_x86());
                }
            }
            return LoadNativeMethodsLegacyNetFramework();
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
            if (!string.IsNullOrEmpty(assemblyLocation))
            {
                return Path.GetDirectoryName(assemblyLocation);
            }
            // In .NET5 single-file deployments, assembly.Location won't be available
            // Also see https://docs.microsoft.com/en-us/dotnet/core/deploying/single-file#other-considerations
            return AppContext.BaseDirectory;
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
