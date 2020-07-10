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

using System.IO;
using System.Text;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Grpc.Tools
{
    // Abstract class for language-specific analysis behavior, such
    // as guessing the generated files the same way protoc does.
    internal abstract class GeneratorServices
    {
        protected readonly TaskLoggingHelper Log;
        protected GeneratorServices(TaskLoggingHelper log) { Log = log; }

        // Obtain a service for the given language (csharp, cpp).
        public static GeneratorServices GetForLanguage(string lang, TaskLoggingHelper log)
        {
            if (lang.EqualNoCase("csharp")) { return new CSharpGeneratorServices(log); }
            if (lang.EqualNoCase("cpp")) { return new CppGeneratorServices(log); }

            log.LogError("Invalid value '{0}' for task property 'Generator'. " +
                "Supported generator languages: CSharp, Cpp.", lang);
            return null;
        }

        // Guess whether item's metadata suggests gRPC stub generation.
        // When "gRPCServices" is not defined, assume gRPC is not used.
        // When defined, C# uses "none" to skip gRPC, C++ uses "false", so
        // recognize both. Since the value is tightly coupled to the scripts,
        // we do not try to validate the value; scripts take care of that.
        // It is safe to assume that gRPC is requested for any other value.
        protected bool GrpcOutputPossible(ITaskItem proto)
        {
            string gsm = proto.GetMetadata(Metadata.GrpcServices);
            return !gsm.EqualNoCase("") && !gsm.EqualNoCase("none")
                && !gsm.EqualNoCase("false");
        }

        // Update OutputDir and GrpcOutputDir for the item and all subsequent
        // targets using this item. This should only be done if the real
        // output directories for protoc should be modified.
        public virtual ITaskItem PatchOutputDirectory(ITaskItem protoItem)
        {
            // Nothing to do
            return protoItem;
        }

        public abstract string[] GetPossibleOutputs(ITaskItem protoItem);

        // Calculate part of proto path relative to root. Protoc is very picky
        // about them matching exactly, so can be we. Expect root be exact prefix
        // to proto, minus some slash normalization.
        protected static string GetRelativeDir(string root, string proto, TaskLoggingHelper log)
        {
            string protoDir = Path.GetDirectoryName(proto);
            string rootDir = EndWithSlash(Path.GetDirectoryName(EndWithSlash(root)));
            if (rootDir == s_dotSlash)
            {
                // Special case, otherwise we can return "./" instead of "" below!
                return protoDir;
            }
            if (Platform.IsFsCaseInsensitive)
            {
                protoDir = protoDir.ToLowerInvariant();
                rootDir = rootDir.ToLowerInvariant();
            }
            protoDir = EndWithSlash(protoDir);
            if (!protoDir.StartsWith(rootDir))
            {
                log.LogWarning("Protobuf item '{0}' has the ProtoRoot metadata '{1}' " +
                               "which is not prefix to its path. Cannot compute relative path.",
                    proto, root);
                return "";
            }
            return protoDir.Substring(rootDir.Length);
        }

        // './' or '.\', normalized per system.
        protected static string s_dotSlash = "." + Path.DirectorySeparatorChar;

        protected static string EndWithSlash(string str)
        {
            if (str == "")
            {
                return s_dotSlash;
            }

            if (str[str.Length - 1] != '\\' && str[str.Length - 1] != '/')
            {
                return str + Path.DirectorySeparatorChar;
            }

            return str;
        }
    };

    // C# generator services.
    internal class CSharpGeneratorServices : GeneratorServices
    {
        public CSharpGeneratorServices(TaskLoggingHelper log) : base(log) { }

        public override ITaskItem PatchOutputDirectory(ITaskItem protoItem)
        {
            var outputItem = new TaskItem(protoItem);
            string root = outputItem.GetMetadata(Metadata.ProtoRoot);
            string proto = outputItem.ItemSpec;
            string relative = GetRelativeDir(root, proto, Log);

            string outdir = outputItem.GetMetadata(Metadata.OutputDir);
            string pathStem = Path.Combine(outdir, relative);
            outputItem.SetMetadata(Metadata.OutputDir, pathStem);

            // Override outdir if GrpcOutputDir present, default to proto output.
            string grpcdir = outputItem.GetMetadata(Metadata.GrpcOutputDir);
            if (grpcdir != "")
            {
                pathStem = Path.Combine(grpcdir, relative);
            }
            outputItem.SetMetadata(Metadata.GrpcOutputDir, pathStem);
            return outputItem;
        }

        public override string[] GetPossibleOutputs(ITaskItem protoItem)
        {
            bool doGrpc = GrpcOutputPossible(protoItem);
            var outputs = new string[doGrpc ? 2 : 1];
            string proto = protoItem.ItemSpec;
            string basename = Path.GetFileNameWithoutExtension(proto);
            string outdir = protoItem.GetMetadata(Metadata.OutputDir);
            string filename = LowerUnderscoreToUpperCamelProtocWay(basename);
            outputs[0] = Path.Combine(outdir, filename) + ".cs";

            if (doGrpc)
            {
                string grpcdir = protoItem.GetMetadata(Metadata.GrpcOutputDir);
                filename = LowerUnderscoreToUpperCamelGrpcWay(basename);
                outputs[1] = Path.Combine(grpcdir, filename) + "Grpc.cs";
            }
            return outputs;
        }

        // This is how the gRPC codegen currently construct its output filename.
        // See src/compiler/generator_helpers.h:118.
        string LowerUnderscoreToUpperCamelGrpcWay(string str)
        {
            var result = new StringBuilder(str.Length, str.Length);
            bool cap = true;
            foreach (char c in str)
            {
                if (c == '_')
                {
                    cap = true;
                }
                else if (cap)
                {
                    result.Append(char.ToUpperInvariant(c));
                    cap = false;
                }
                else
                {
                    result.Append(c);
                }
            }
            return result.ToString();
        }

        // This is how the protoc codegen constructs its output filename.
        // See protobuf/compiler/csharp/csharp_helpers.cc:137.
        // Note that protoc explicitly discards non-ASCII letters.
        string LowerUnderscoreToUpperCamelProtocWay(string str)
        {
            var result = new StringBuilder(str.Length, str.Length);
            bool cap = true;
            foreach (char c in str)
            {
                char upperC = char.ToUpperInvariant(c);
                bool isAsciiLetter = 'A' <= upperC && upperC <= 'Z';
                if (isAsciiLetter || ('0' <= c && c <= '9'))
                {
                    result.Append(cap ? upperC : c);
                }
                cap = !isAsciiLetter;
            }
            return result.ToString();
        }
    };

    // C++ generator services.
    internal class CppGeneratorServices : GeneratorServices
    {
        public CppGeneratorServices(TaskLoggingHelper log) : base(log) { }

        public override string[] GetPossibleOutputs(ITaskItem protoItem)
        {
            bool doGrpc = GrpcOutputPossible(protoItem);
            string root = protoItem.GetMetadata(Metadata.ProtoRoot);
            string proto = protoItem.ItemSpec;
            string filename = Path.GetFileNameWithoutExtension(proto);
            // E. g., ("foo/", "foo/bar/x.proto") => "bar"
            string relative = GetRelativeDir(root, proto, Log);

            var outputs = new string[doGrpc ? 4 : 2];
            string outdir = protoItem.GetMetadata(Metadata.OutputDir);
            string fileStem = Path.Combine(outdir, relative, filename);
            outputs[0] = fileStem + ".pb.cc";
            outputs[1] = fileStem + ".pb.h";
            if (doGrpc)
            {
                // Override outdir if GrpcOutputDir present, default to proto output.
                outdir = protoItem.GetMetadata(Metadata.GrpcOutputDir);
                if (outdir != "")
                {
                    fileStem = Path.Combine(outdir, relative, filename);
                }
                outputs[2] = fileStem + ".grpc.pb.cc";
                outputs[3] = fileStem + ".grpc.pb.h";
            }
            return outputs;
        }
    }
}
