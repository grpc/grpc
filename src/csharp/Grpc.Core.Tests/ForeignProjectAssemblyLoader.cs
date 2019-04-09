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


#if NETCOREAPP1_1 || NETCOREAPP2_1

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using Microsoft.Extensions.DependencyModel;
using Microsoft.Extensions.DependencyModel.Resolution;
using Newtonsoft.Json;
using NUnit.Framework;
using System.Linq;
using System.Globalization;

namespace Grpc.Core.Tests
{
    /// <summary>
    /// Loads an assembly from a foreign project and makes sure its dependencies
    /// are resolved accordingly.
    /// This class is only useful on .NET Core, in .NET desktop one can simply
    /// use <c>Assembly.LoadFrom()</c> and the dependency resolution will work
    /// just fine.
    /// </summary>
    internal class ForeignProjectAssemblyLoader
    {
        private readonly AssemblyLoadContext loadContext;
        private readonly Dictionary<string, string> resolvedAssemblyPaths;

        public ForeignProjectAssemblyLoader(string appBasePath, string assemblyPath)
        {
            this.loadContext = new CustomAssemblyLoadContext();
            this.Assembly = loadContext.LoadFromAssemblyPath(assemblyPath);
            this.resolvedAssemblyPaths = ResolveDependencyAssemblyPaths(appBasePath, this.Assembly);
            this.loadContext.Resolving += OnResolving;
        }

        public Assembly Assembly { get; }

        private Assembly OnResolving(AssemblyLoadContext context, AssemblyName name)
        {
            var lowerCaseAssemblyName = name.Name.ToLower(CultureInfo.InvariantCulture);

            if (resolvedAssemblyPaths.TryGetValue(lowerCaseAssemblyName, out string assemblyPath))
            {
                return this.loadContext.LoadFromAssemblyPath(assemblyPath);
            }
            return null;
        }

        private static Dictionary<string, string> ResolveDependencyAssemblyPaths(string appBasePath, Assembly assembly)
        {
            var dependencyContext = DependencyContext.Load(assembly);

            // use assembly resolver logic that is similar to when the foreign
            // project is run independently
            var assemblyResolver = new CompositeCompilationAssemblyResolver(new ICompilationAssemblyResolver[]
            {
                new AppBaseCompilationAssemblyResolver(appBasePath),
                new ReferenceAssemblyPathResolver(),
                new PackageCompilationAssemblyResolver()
            });

            var assemblyPaths = new List<string>();
            foreach (var runtimeLibrary in dependencyContext.RuntimeLibraries)
            {
                var compilationLibrary = new CompilationLibrary(
                    runtimeLibrary.Type,
                    runtimeLibrary.Name,
                    runtimeLibrary.Version,
                    runtimeLibrary.Hash,
                    runtimeLibrary.RuntimeAssemblyGroups.SelectMany(g => g.AssetPaths),
                    runtimeLibrary.Dependencies,
                    runtimeLibrary.Serviceable);

                assemblyResolver.TryResolveAssemblyPaths(compilationLibrary, assemblyPaths);
            }

            var result = new Dictionary<string, string>();
            foreach (var assemblyPath in assemblyPaths)
            {
                var lowerCaseAssemblyName = Path.GetFileName(assemblyPath).ToLower(CultureInfo.InvariantCulture);
                if (lowerCaseAssemblyName.EndsWith(".dll", StringComparison.InvariantCultureIgnoreCase))
                {
                    lowerCaseAssemblyName = lowerCaseAssemblyName.Substring(0, lowerCaseAssemblyName.Length - 4);
                }
                result[lowerCaseAssemblyName] = assemblyPath;
            }
            return result;
        }

        private class CustomAssemblyLoadContext : AssemblyLoadContext
        {
            protected override Assembly Load(AssemblyName assemblyName)
            {
                return null;  // allow fallback to framework libraries
            }
        }
    }
}
#endif
