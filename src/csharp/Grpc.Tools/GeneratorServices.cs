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
using System.Text;
using System.Text.RegularExpressions;
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

        public abstract string[] GetPossibleOutputs(ITaskItem proto);
    };

    // C# generator services.
    internal class CSharpGeneratorServices : GeneratorServices
    {
        private static readonly Regex s_namespaceRegex = new Regex("option csharp_namespace = \"(\\S+)\";", RegexOptions.Compiled);
        private static readonly Regex s_packageRegex = new Regex("package (\\S+);", RegexOptions.Compiled);

        public CSharpGeneratorServices(TaskLoggingHelper log) : base(log) { }

        public override string[] GetPossibleOutputs(ITaskItem protoItem)
        {
            bool doGrpc = GrpcOutputPossible(protoItem);
            var outputs = new string[doGrpc ? 2 : 1];
            var itemSpec = protoItem.ItemSpec;
            string outdir = protoItem.GetMetadata(Metadata.OutputDir);
            string basename = Path.GetFileNameWithoutExtension(itemSpec);
            string filename = UnderscoresToPascalCase(basename);

            if ("true".EqualNoCase(protoItem.GetMetadata(Metadata.BaseNamespaceEnabled)))
            {
                string baseNamespace = protoItem.GetMetadata(Metadata.BaseNamespace);
                string baseNamespaceDir = GetNamespaceDir(itemSpec, baseNamespace);
                if (baseNamespaceDir != null)
                {
                    filename = Path.Combine(baseNamespaceDir, filename);
                }
            }

            outputs[0] = Path.Combine(outdir, filename) + ".cs";

            if (doGrpc)
            {
                // Override outdir if kGrpcOutputDir present, default to proto output.
                string grpcdir = protoItem.GetMetadata(Metadata.GrpcOutputDir);
                outdir = grpcdir != "" ? grpcdir : outdir;
                outputs[1] = Path.Combine(outdir, filename) + "Grpc.cs";
            }
            return outputs;
        }

        string GetNamespaceDir(string filename, string baseNamespace)
        {
            if (!File.Exists(filename)) return null;

            string ns = GetFileNamespace(filename);
            if (ns == null) return null;

            string namespace_suffix = ns;
            if (!string.IsNullOrEmpty(baseNamespace)) {
                // Check that the base_namespace is either equal to or a leading part of
                // the file namespace. This isn't just a simple prefix; "Foo.B" shouldn't
                // be regarded as a prefix of "Foo.Bar". The simplest option is to add "."
                // to both.
                string extended_ns = ns + ".";
                if (extended_ns.IndexOf(baseNamespace + ".", StringComparison.Ordinal) != 0) {
                    return null; // This will be ignored, because we've set an error.
                }

                namespace_suffix = ns.Substring(baseNamespace.Length);
                if (namespace_suffix.IndexOf(".", StringComparison.Ordinal) == 0) {
                    namespace_suffix = namespace_suffix.Substring(1);
                }
            }

            return namespace_suffix.Replace('.', Path.DirectorySeparatorChar);
        }

        private string GetFileNamespace(string filename)
        {
            string data = File.ReadAllText(filename);

            // First try to match the namespace from file options.
            Match match = s_namespaceRegex.Match(data);
            if (match.Success) return match.Groups[1].Value;

            // After that, match the package.
            match = s_packageRegex.Match(data);
            if (!match.Success) return null;

            // Convert the package name to C# notation.
            return UnderscoresToCamelCase(match.Groups[1].Value, true, true);
        }

        private string UnderscoresToPascalCase(string input) {
            return UnderscoresToCamelCase(input, true);
        }

        // This is how the protoc codegen constructs its output filename.
        // See protobuf/compiler/csharp/csharp_helpers.cc:143.
        // Note that protoc explicitly discards non-ASCII letters.
        private string UnderscoresToCamelCase(
            string input,
            bool capNextLetter = false,
            bool preservePeriod = false)
        {
            var result = new StringBuilder(input.Length, input.Length);
            foreach (char c in input)
            {
                char upperC = char.ToUpperInvariant(c);
                bool isAsciiLetter = 'A' <= upperC && upperC <= 'Z';
                if (isAsciiLetter || ('0' <= c && c <= '9'))
                {
                    result.Append(capNextLetter ? upperC : c);
                }
                else if (c == '.' && preservePeriod)
                {
                    result.Append(c);
                }

                capNextLetter = !isAsciiLetter;
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
            string relative = GetRelativeDir(root, proto);

            var outputs = new string[doGrpc ? 4 : 2];
            string outdir = protoItem.GetMetadata(Metadata.OutputDir);
            string fileStem = Path.Combine(outdir, relative, filename);
            outputs[0] = fileStem + ".pb.cc";
            outputs[1] = fileStem + ".pb.h";
            if (doGrpc)
            {
                // Override outdir if kGrpcOutputDir present, default to proto output.
                outdir = protoItem.GetMetadata(Metadata.GrpcOutputDir);
                if (outdir != "")
                {
                    fileStem = Path.Combine(outdir, relative, filename);
                }
                outputs[2] = fileStem + "_grpc.pb.cc";
                outputs[3] = fileStem + "_grpc.pb.h";
            }
            return outputs;
        }

        // Calculate part of proto path relative to root. Protoc is very picky
        // about them matching exactly, so can be we. Expect root be exact prefix
        // to proto, minus some slash normalization.
        string GetRelativeDir(string root, string proto)
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
                Log.LogWarning("Protobuf item '{0}' has the ProtoRoot metadata '{1}' " +
                  "which is not prefix to its path. Cannot compute relative path.",
                  proto, root);
                return "";
            }
            return protoDir.Substring(rootDir.Length);
        }

        // './' or '.\', normalized per system.
        static string s_dotSlash = "." + Path.DirectorySeparatorChar;

        static string EndWithSlash(string str)
        {
            if (str == "")
            {
                return s_dotSlash;
            }
            else if (str[str.Length - 1] != '\\' && str[str.Length - 1] != '/')
            {
                return str + Path.DirectorySeparatorChar;
            }
            else
            {
                return str;
            }
        }
    };
}
