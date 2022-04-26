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

using System.Collections.Generic;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Grpc.Tools
{
    public class ProtoCompilerOutputs : Task
    {
        /// <summary>
        /// Code generator. Currently supported are "csharp", "cpp".
        /// </summary>
        [Required]
        public string Generator { get; set; }

        /// <summary>
        /// All Proto files in the project. The task computes possible outputs
        /// from these proto files, and returns them in the PossibleOutputs list.
        /// Not all of these might be actually produced by protoc; this is dealt
        /// with later in the ProtoCompile task which returns the list of
        /// files actually produced by the compiler.
        /// </summary>
        [Required]
        public ITaskItem[] Protobuf { get; set; }

        /// <summary>
        /// All Proto files in the project. A patched copy of all items from
        /// Protobuf that might contain updated OutputDir and GrpcOutputDir
        /// attributes.
        /// </summary>
        [Output]
        public ITaskItem[] PatchedProtobuf { get; set; }

        /// <summary>
        /// Output items per each potential output. We do not look at existing
        /// cached dependency even if they exist, since file may be refactored,
        /// affecting whether or not gRPC code file is generated from a given proto.
        /// Instead, all potentially possible generated sources are collected.
        /// It is a wise idea to generate empty files later for those potentials
        /// that are not actually created by protoc, so the dependency checks
        /// result in a minimal recompilation. The Protoc task can output the
        /// list of files it actually produces, given right combination of its
        /// properties.
        /// Output items will have the Source metadata set on them:
        ///     <ItemName Include="MyProto.cs" Source="my_proto.proto" />
        /// </summary>
        [Output]
        public ITaskItem[] PossibleOutputs { get; private set; }

        public override bool Execute()
        {
            var generator = GeneratorServices.GetForLanguage(Generator, Log);
            if (generator == null)
            {
                // Error already logged, just return.
                return false;
            }

            // Get language-specific possible output. The generator expects certain
            // metadata be set on the proto item.
            var possible = new List<ITaskItem>();
            var patched = new List<ITaskItem>();
            foreach (var proto in Protobuf)
            {
                var patchedProto = generator.PatchOutputDirectory(proto);
                patched.Add(patchedProto);

                var outputs = generator.GetPossibleOutputs(patchedProto);
                foreach (string output in outputs)
                {
                    var ti = new TaskItem(output);
                    ti.SetMetadata(Metadata.Source, patchedProto.ItemSpec);
                    possible.Add(ti);
                }
            }

            PatchedProtobuf = patched.ToArray();
            PossibleOutputs = possible.ToArray();

            return !Log.HasLoggedErrors;
        }
    };
}
