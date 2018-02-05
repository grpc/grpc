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
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Grpc.Tools {
  // Abstract class for language-specific analysis behavior, such
  // as guessing the generated files the same way protoc does.
  internal abstract class GeneratorServices {
    protected readonly TaskLoggingHelper Log;
    protected GeneratorServices(TaskLoggingHelper log) {
      Log = log;
    }

    // Obtain a service for the given language (csharp, cpp).
    public static GeneratorServices GetForLanguage(string lang, TaskLoggingHelper log) {
      if (lang.EqualNoCase("csharp"))
        return new CSharpGeneratorServices(log);
      if (lang.EqualNoCase("cpp"))
        return new CppGeneratorServices(log);
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
    protected bool GrpcOutputPossible(ITaskItem proto) {
      string gsm = proto.GetMetadata(Metadata.kGrpcServices);
      return !gsm.EqualNoCase("") && !gsm.EqualNoCase("none")
          && !gsm.EqualNoCase("false");
    }

    public abstract string[] GetPossibleOutputs(ITaskItem proto);
  };

  // C# generator services.
  internal class CSharpGeneratorServices : GeneratorServices {
    public CSharpGeneratorServices(TaskLoggingHelper log) : base(log) {}

    public override string[] GetPossibleOutputs(ITaskItem protoItem) {
      bool doGrpc = GrpcOutputPossible(protoItem);
      string filename = LowerUnderscoreToUpperCamel(
        Path.GetFileNameWithoutExtension(protoItem.ItemSpec));

      var outputs = new string[doGrpc ? 2 : 1];
      string outdir = protoItem.GetMetadata(Metadata.kOutputDir);
      string fileStem = Path.Combine(outdir, filename);
      outputs[0] = fileStem + ".cs";
      if (doGrpc) {
        // Override outdir if kGrpcOutputDir present, default to proto output.
        outdir = protoItem.GetMetadata(Metadata.kGrpcOutputDir);
        if (outdir != "") {
          fileStem = Path.Combine(outdir, filename);
        }
        outputs[1] = fileStem + "Grpc.cs";
      }
      return outputs;
    }

    string LowerUnderscoreToUpperCamel(string str) {
      // See src/compiler/generator_helpers.h:118
      var result = new StringBuilder(str.Length, str.Length);
      bool cap = true;
      foreach (char c in str) {
        if (c == '_') {
          cap = true;
        } else if (cap) {
          result.Append(char.ToUpperInvariant(c));
          cap = false;
        } else {
          result.Append(c);
        }
      }
      return result.ToString();
    }
  };

  // C++ generator services.
  internal class CppGeneratorServices : GeneratorServices {
    public CppGeneratorServices(TaskLoggingHelper log) : base(log) { }

    public override string[] GetPossibleOutputs(ITaskItem protoItem) {
      bool doGrpc = GrpcOutputPossible(protoItem);
      string root = protoItem.GetMetadata(Metadata.kProtoRoot);
      string proto = protoItem.ItemSpec;
      string filename = Path.GetFileNameWithoutExtension(proto);
      // E. g., ("foo/", "foo/bar/x.proto") => "bar"
      string relative = GetRelativeDir(root, proto);

      var outputs = new string[doGrpc ? 4 : 2];
      string outdir = protoItem.GetMetadata(Metadata.kOutputDir);
      string fileStem = Path.Combine(outdir, relative, filename);
      outputs[0] = fileStem + ".pb.cc";
      outputs[1] = fileStem + ".pb.h";
      if (doGrpc) {
        // Override outdir if kGrpcOutputDir present, default to proto output.
        outdir = protoItem.GetMetadata(Metadata.kGrpcOutputDir);
        if (outdir != "") {
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
    string GetRelativeDir(string root, string proto) {
      string protoDir = Path.GetDirectoryName(proto);
      string rootDir = EndWithSlash(Path.GetDirectoryName(EndWithSlash(root)));
      if (rootDir == s_dotSlash) {
        // Special case, otherwise we can return "./" instead of "" below!
        return protoDir;
      }
      if (Platform.IsFsCaseInsensitive) {
        protoDir = protoDir.ToLowerInvariant();
        rootDir = rootDir.ToLowerInvariant();
      }
      protoDir = EndWithSlash(protoDir);
      if (!protoDir.StartsWith(rootDir)) {
        Log.LogWarning("ProtoBuf item '{0}' has the ProtoRoot metadata '{1}' " +
          "which is not prefix to its path. Cannot compute relative path.",
          proto, root);
        return "";
      }
      return protoDir.Substring(rootDir.Length);
    }

    // './' or '.\', normalized per system.
    static string s_dotSlash = "." + Path.DirectorySeparatorChar;

    static string EndWithSlash(string str) {
      if (str == "") {
        return s_dotSlash;
      } else if (str[str.Length - 1] != '\\' && str[str.Length - 1] != '/') {
        return str + Path.DirectorySeparatorChar;
      } else {
        return str;
      }
    }
  };
}
