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

using Grpc.Reflection;
using Grpc.Reflection.V1Alpha;
using NUnit.Framework;


namespace Grpc.Reflection.Tests
{
    /// <summary>
    /// Tests for ReflectionServiceImpl
    /// </summary>
    public class SymbolRegistryTest
    {
        SymbolRegistry registry = SymbolRegistry.FromFiles(new[] { ReflectionReflection.Descriptor, Google.Protobuf.WellKnownTypes.Duration.Descriptor.File });

        [Test]
        public void FileByName()
        {
            Assert.AreSame(Google.Protobuf.WellKnownTypes.Duration.Descriptor.File, registry.FileByName("google/protobuf/duration.proto"));
            Assert.IsNull(registry.FileByName("somepackage/nonexistent.proto"));
        }

        [Test]
        public void FileContainingSymbol()
        {
            Assert.AreSame(Google.Protobuf.WellKnownTypes.Duration.Descriptor.File, registry.FileContainingSymbol("google.protobuf.Duration"));
            Assert.AreSame(ReflectionReflection.Descriptor, registry.FileContainingSymbol("grpc.reflection.v1alpha.ServerReflection.ServerReflectionInfo"));  // method
            Assert.AreSame(ReflectionReflection.Descriptor, registry.FileContainingSymbol("grpc.reflection.v1alpha.ServerReflection"));  // service
            Assert.AreSame(ReflectionReflection.Descriptor, registry.FileContainingSymbol("grpc.reflection.v1alpha.ServerReflectionRequest"));  // message
            Assert.IsNull(registry.FileContainingSymbol("somepackage.Nonexistent"));
        }
    }
}
