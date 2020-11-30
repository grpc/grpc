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
            return LoadNativeMethodsDefault();
        }

        /// <summary>
        /// Return native method delegates when running on .NET Core or .NET Framework.
        /// </summary>
        private static NativeMethods LoadNativeMethodsDefault()
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

            // on Windows, the DllImport("grpc_csharp_ext.x64") doesn't always work,
            // but DllImport("grpc_csharp_ext.x64.dll") does, so we need a special case for that.
            // See https://github.com/dotnet/coreclr/pull/17505
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
    }
}
