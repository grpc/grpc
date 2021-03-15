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
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Grpc.Tools
{
    /// <summary>
    /// Run Google proto compiler (protoc).
    ///
    /// After a successful run, the task reads the dependency file if specified
    /// to be saved by the compiler, and returns its output files.
    ///
    /// This task (unlike PrepareProtoCompile) does not attempt to guess anything
    /// about language-specific behavior of protoc, and therefore can be used for
    /// any language outputs.
    /// </summary>
    public class ProtoCompile : ToolTask
    {
        /*

        Usage: /home/kkm/work/protobuf/src/.libs/lt-protoc [OPTION] PROTO_FILES
        Parse PROTO_FILES and generate output based on the options given:
          -IPATH, --proto_path=PATH   Specify the directory in which to search for
                                      imports.  May be specified multiple times;
                                      directories will be searched in order.  If not
                                      given, the current working directory is used.
          --version                   Show version info and exit.
          -h, --help                  Show this text and exit.
          --encode=MESSAGE_TYPE       Read a text-format message of the given type
                                      from standard input and write it in binary
                                      to standard output.  The message type must
                                      be defined in PROTO_FILES or their imports.
          --decode=MESSAGE_TYPE       Read a binary message of the given type from
                                      standard input and write it in text format
                                      to standard output.  The message type must
                                      be defined in PROTO_FILES or their imports.
          --decode_raw                Read an arbitrary protocol message from
                                      standard input and write the raw tag/value
                                      pairs in text format to standard output.  No
                                      PROTO_FILES should be given when using this
                                      flag.
          --descriptor_set_in=FILES   Specifies a delimited list of FILES
                                      each containing a FileDescriptorSet (a
                                      protocol buffer defined in descriptor.proto).
                                      The FileDescriptor for each of the PROTO_FILES
                                      provided will be loaded from these
                                      FileDescriptorSets. If a FileDescriptor
                                      appears multiple times, the first occurrence
                                      will be used.
          -oFILE,                     Writes a FileDescriptorSet (a protocol buffer,
            --descriptor_set_out=FILE defined in descriptor.proto) containing all of
                                      the input files to FILE.
          --include_imports           When using --descriptor_set_out, also include
                                      all dependencies of the input files in the
                                      set, so that the set is self-contained.
          --include_source_info       When using --descriptor_set_out, do not strip
                                      SourceCodeInfo from the FileDescriptorProto.
                                      This results in vastly larger descriptors that
                                      include information about the original
                                      location of each decl in the source file as
                                      well as surrounding comments.
          --dependency_out=FILE       Write a dependency output file in the format
                                      expected by make. This writes the transitive
                                      set of input file paths to FILE
          --error_format=FORMAT       Set the format in which to print errors.
                                      FORMAT may be 'gcc' (the default) or 'msvs'
                                      (Microsoft Visual Studio format).
          --print_free_field_numbers  Print the free field numbers of the messages
                                      defined in the given proto files. Groups share
                                      the same field number space with the parent
                                      message. Extension ranges are counted as
                                      occupied fields numbers.

          --plugin=EXECUTABLE         Specifies a plugin executable to use.
                                      Normally, protoc searches the PATH for
                                      plugins, but you may specify additional
                                      executables not in the path using this flag.
                                      Additionally, EXECUTABLE may be of the form
                                      NAME=PATH, in which case the given plugin name
                                      is mapped to the given executable even if
                                      the executable's own name differs.
          --cpp_out=OUT_DIR           Generate C++ header and source.
          --csharp_out=OUT_DIR        Generate C# source file.
          --java_out=OUT_DIR          Generate Java source file.
          --javanano_out=OUT_DIR      Generate Java Nano source file.
          --js_out=OUT_DIR            Generate JavaScript source.
          --objc_out=OUT_DIR          Generate Objective C header and source.
          --php_out=OUT_DIR           Generate PHP source file.
          --python_out=OUT_DIR        Generate Python source file.
          --ruby_out=OUT_DIR          Generate Ruby source file.
          @<filename>                 Read options and filenames from file. If a
                                      relative file path is specified, the file
                                      will be searched in the working directory.
                                      The --proto_path option will not affect how
                                      this argument file is searched. Content of
                                      the file will be expanded in the position of
                                      @<filename> as in the argument list. Note
                                      that shell expansion is not applied to the
                                      content of the file (i.e., you cannot use
                                      quotes, wildcards, escapes, commands, etc.).
                                      Each line corresponds to a single argument,
                                      even if it contains spaces.
        */
        static string[] s_supportedGenerators = new[] { "cpp", "csharp", "java",
                                                        "javanano", "js", "objc",
                                                        "php", "python", "ruby" };

        static readonly TimeSpan s_regexTimeout = TimeSpan.FromMilliseconds(100);

        static readonly List<ErrorListFilter> s_errorListFilters = new List<ErrorListFilter>()
        {
            // Example warning with location
            //../Protos/greet.proto(19) : warning in column=5 : warning : When enum name is stripped and label is PascalCased (Zero),
            // this value label conflicts with Zero. This will make the proto fail to compile for some languages, such as C#.
            new ErrorListFilter
            {
                Pattern = new Regex(
                    pattern: "^(?'FILENAME'.+?)\\((?'LINE'\\d+)\\) ?: ?warning in column=(?'COLUMN'\\d+) ?: ?(?'TEXT'.*)",
                    options: RegexOptions.Compiled | RegexOptions.IgnoreCase,
                    matchTimeout: s_regexTimeout),
                LogAction = (log, match) =>
                {
                    int.TryParse(match.Groups["LINE"].Value, out var line);
                    int.TryParse(match.Groups["COLUMN"].Value, out var column);

                    log.LogWarning(
                        subcategory: null,
                        warningCode: null,
                        helpKeyword: null,
                        file: match.Groups["FILENAME"].Value,
                        lineNumber: line,
                        columnNumber: column,
                        endLineNumber: 0,
                        endColumnNumber: 0,
                        message: match.Groups["TEXT"].Value);
                }
            },

            // Example error with location
            //../Protos/greet.proto(14) : error in column=10: "name" is already defined in "Greet.HelloRequest".
            new ErrorListFilter
            {
                Pattern = new Regex(
                    pattern: "^(?'FILENAME'.+?)\\((?'LINE'\\d+)\\) ?: ?error in column=(?'COLUMN'\\d+) ?: ?(?'TEXT'.*)",
                    options: RegexOptions.Compiled | RegexOptions.IgnoreCase,
                    matchTimeout: s_regexTimeout),
                LogAction = (log, match) =>
                {
                    int.TryParse(match.Groups["LINE"].Value, out var line);
                    int.TryParse(match.Groups["COLUMN"].Value, out var column);

                    log.LogError(
                        subcategory: null,
                        errorCode: null,
                        helpKeyword: null,
                        file: match.Groups["FILENAME"].Value,
                        lineNumber: line,
                        columnNumber: column,
                        endLineNumber: 0,
                        endColumnNumber: 0,
                        message: match.Groups["TEXT"].Value);
                }
            },

            // Example warning without location
            //../Protos/greet.proto: warning: Import google/protobuf/empty.proto but not used.
            new ErrorListFilter
            {
                Pattern = new Regex(
                    pattern: "^(?'FILENAME'.+?): ?warning: ?(?'TEXT'.*)",
                    options: RegexOptions.Compiled | RegexOptions.IgnoreCase,
                    matchTimeout: s_regexTimeout),
                LogAction = (log, match) =>
                {
                    log.LogWarning(
                        subcategory: null,
                        warningCode: null,
                        helpKeyword: null,
                        file: match.Groups["FILENAME"].Value,
                        lineNumber: 0,
                        columnNumber: 0,
                        endLineNumber: 0,
                        endColumnNumber: 0,
                        message: match.Groups["TEXT"].Value);
                }
            },

            // Example error without location
            //../Protos/greet.proto: Import "google/protobuf/empty.proto" was listed twice.
            new ErrorListFilter
            {
                Pattern = new Regex(
                    pattern: "^(?'FILENAME'.+?): ?(?'TEXT'.*)",
                    options: RegexOptions.Compiled | RegexOptions.IgnoreCase,
                    matchTimeout: s_regexTimeout),
                LogAction = (log, match) =>
                {
                    log.LogError(
                        subcategory: null,
                        errorCode: null,
                        helpKeyword: null,
                        file: match.Groups["FILENAME"].Value,
                        lineNumber: 0,
                        columnNumber: 0,
                        endLineNumber: 0,
                        endColumnNumber: 0,
                        message: match.Groups["TEXT"].Value);
                }
            }
        };

        /// <summary>
        /// Code generator.
        /// </summary>
        [Required]
        public string Generator { get; set; }

        /// <summary>
        /// Protobuf files to compile.
        /// </summary>
        [Required]
        public ITaskItem[] Protobuf { get; set; }

        /// <summary>
        /// Directory where protoc dependency files are cached. If provided, dependency
        /// output filename is autogenerated from source directory hash and file name.
        /// Mutually exclusive with DependencyOut.
        /// Switch: --dependency_out (with autogenerated file name).
        /// </summary>
        public string ProtoDepDir { get; set; }

        /// <summary>
        /// Dependency file full name. Mutually exclusive with ProtoDepDir.
        /// Autogenerated file name is available in this property after execution.
        /// Switch: --dependency_out.
        /// </summary>
        [Output]
        public string DependencyOut { get; set; }

        /// <summary>
        /// The directories to search for imports. Directories will be searched
        /// in order. If not given, the current working directory is used.
        /// Switch: --proto_path.
        /// </summary>
        public string[] ProtoPath { get; set; }

        /// <summary>
        /// Generated code directory. The generator property determines the language.
        /// Switch: --GEN_out= (for different generators GEN, e.g. --csharp_out).
        /// </summary>
        [Required]
        public string OutputDir { get; set; }

        /// <summary>
        /// Codegen options. See also OptionsFromMetadata.
        /// Switch: --GEN_opt= (for different generators GEN, e.g. --csharp_opt).
        /// </summary>
        public string[] OutputOptions { get; set; }

        /// <summary>
        /// Additional arguments that will be passed unmodified to protoc (and before any file names).
        /// For example, "--experimental_allow_proto3_optional"
        /// </summary>
        public string[] AdditionalProtocArguments { get; set; }

        /// <summary>
        /// Full path to the gRPC plugin executable. If specified, gRPC generation
        /// is enabled for the files.
        /// Switch: --plugin=protoc-gen-grpc=
        /// </summary>
        public string GrpcPluginExe { get; set; }

        /// <summary>
        /// Generated gRPC  directory. The generator property determines the
        /// language. If gRPC is enabled but this is not given, OutputDir is used.
        /// Switch: --grpc_out=
        /// </summary>
        public string GrpcOutputDir { get; set; }

        /// <summary>
        /// gRPC Codegen options. See also OptionsFromMetadata.
        /// --grpc_opt=opt1,opt2=val (comma-separated).
        /// </summary>
        public string[] GrpcOutputOptions { get; set; }

        /// <summary>
        /// List of files written in addition to generated outputs. Includes a
        /// single item for the dependency file if written.
        /// </summary>
        [Output]
        public ITaskItem[] AdditionalFileWrites { get; private set; }

        /// <summary>
        /// List of language files generated by protoc. Empty unless DependencyOut
        /// or ProtoDepDir is set, since the file writes are extracted from protoc
        /// dependency output file.
        /// </summary>
        [Output]
        public ITaskItem[] GeneratedFiles { get; private set; }

        // Hide this property from MSBuild, we should never use a shell script.
        private new bool UseCommandProcessor { get; set; }

        protected override string ToolName => Platform.IsWindows ? "protoc.exe" : "protoc";

        // Since we never try to really locate protoc.exe somehow, just try ToolExe
        // as the full tool location. It will be either just protoc[.exe] from
        // ToolName above if not set by the user, or a user-supplied full path. The
        // base class will then resolve the former using system PATH.
        protected override string GenerateFullPathToTool() => ToolExe;

        // Log protoc errors with the High priority (bold white in MsBuild,
        // printed with -v:n, and shown in the Output windows in VS).
        protected override MessageImportance StandardErrorLoggingImportance => MessageImportance.High;

        // Called by base class to validate arguments and make them consistent.
        protected override bool ValidateParameters()
        {
            // Part of proto command line switches, must be lowercased.
            Generator = Generator.ToLowerInvariant();
            if (!System.Array.Exists(s_supportedGenerators, g => g == Generator))
            {
                Log.LogError("Invalid value for Generator='{0}'. Supported generators: {1}",
                             Generator, string.Join(", ", s_supportedGenerators));
            }

            if (ProtoDepDir != null && DependencyOut != null)
            {
                Log.LogError("Properties ProtoDepDir and DependencyOut may not be both specified");
            }

            if (Protobuf.Length > 1 && (ProtoDepDir != null || DependencyOut != null))
            {
                Log.LogError("Proto compiler currently allows only one input when " +
                             "--dependency_out is specified (via ProtoDepDir or DependencyOut). " +
                             "Tracking issue: https://github.com/google/protobuf/pull/3959");
            }

            // Use ProtoDepDir to autogenerate DependencyOut
            if (ProtoDepDir != null)
            {
                DependencyOut = DepFileUtil.GetDepFilenameForProto(ProtoDepDir, Protobuf[0].ItemSpec);
            }

            if (GrpcPluginExe == null)
            {
                GrpcOutputOptions = null;
                GrpcOutputDir = null;
            }
            else if (GrpcOutputDir == null)
            {
                // Use OutputDir for gRPC output if not specified otherwise by user.
                GrpcOutputDir = OutputDir;
            }

            return !Log.HasLoggedErrors && base.ValidateParameters();
        }

        // Protoc chokes on BOM, naturally. I would!
        static readonly Encoding s_utf8WithoutBom = new UTF8Encoding(false);
        protected override Encoding ResponseFileEncoding => s_utf8WithoutBom;

        // Protoc takes one argument per line from the response file, and does not
        // require any quoting whatsoever. Otherwise, this is similar to the
        // standard CommandLineBuilder
        class ProtocResponseFileBuilder
        {
            StringBuilder _data = new StringBuilder(1000);
            public override string ToString() => _data.ToString();

            // If 'value' is not empty, append '--name=value\n'.
            public void AddSwitchMaybe(string name, string value)
            {
                if (!string.IsNullOrEmpty(value))
                {
                    _data.Append("--").Append(name).Append("=")
                         .Append(value).Append('\n');
                }
            }

            // Add switch with the 'values' separated by commas, for options.
            public void AddSwitchMaybe(string name, string[] values)
            {
                if (values?.Length > 0)
                {
                    _data.Append("--").Append(name).Append("=")
                         .Append(string.Join(",", values)).Append('\n');
                }
            }

            // Add a positional argument to the file data.
            public void AddArg(string arg)
            {
                _data.Append(arg).Append('\n');
            }
        };

        // Called by the base ToolTask to get response file contents.
        protected override string GenerateResponseFileCommands()
        {
            var cmd = new ProtocResponseFileBuilder();
            cmd.AddSwitchMaybe(Generator + "_out", TrimEndSlash(OutputDir));
            cmd.AddSwitchMaybe(Generator + "_opt", OutputOptions);
            cmd.AddSwitchMaybe("plugin=protoc-gen-grpc", GrpcPluginExe);
            cmd.AddSwitchMaybe("grpc_out", TrimEndSlash(GrpcOutputDir));
            cmd.AddSwitchMaybe("grpc_opt", GrpcOutputOptions);
            if (ProtoPath != null)
            {
                foreach (string path in ProtoPath)
                {
                    cmd.AddSwitchMaybe("proto_path", TrimEndSlash(path));
                }
            }
            cmd.AddSwitchMaybe("dependency_out", DependencyOut);
            cmd.AddSwitchMaybe("error_format", "msvs");

            if (AdditionalProtocArguments != null)
            {
                foreach (var additionalProtocOption in AdditionalProtocArguments)
                {
                    cmd.AddArg(additionalProtocOption);
                }
            }

            foreach (var proto in Protobuf)
            {
                cmd.AddArg(proto.ItemSpec);
            }
            return cmd.ToString();
        }

        // Protoc cannot digest trailing slashes in directory names,
        // curiously under Linux, but not in Windows.
        static string TrimEndSlash(string dir)
        {
            if (dir == null || dir.Length <= 1)
            {
                return dir;
            }
            string trim = dir.TrimEnd('/', '\\');
            // Do not trim the root slash, drive letter possible.
            if (trim.Length == 0)
            {
                // Slashes all the way down.
                return dir.Substring(0, 1);
            }
            if (trim.Length == 2 && dir.Length > 2 && trim[1] == ':')
            {
                // We have a drive letter and root, e. g. 'C:\'
                return dir.Substring(0, 3);
            }
            return trim;
        }

        // Called by the base class to log tool's command line.
        //
        // Protoc command file is peculiar, with one argument per line, separated
        // by newlines. Unwrap it for log readability into a single line, and also
        // quote arguments, lest it look weird and so it may be copied and pasted
        // into shell. Since this is for logging only, correct enough is correct.
        protected override void LogToolCommand(string cmd)
        {
            var printer = new StringBuilder(1024);

            // Print 'str' slice into 'printer', wrapping in quotes if contains some
            // interesting characters in file names, or if empty string. The list of
            // characters requiring quoting is not by any means exhaustive; we are
            // just striving to be nice, not guaranteeing to be nice.
            var quotable = new[] { ' ', '!', '$', '&', '\'', '^' };
            void PrintQuoting(string str, int start, int count)
            {
                bool wrap = count == 0 || str.IndexOfAny(quotable, start, count) >= 0;
                if (wrap) printer.Append('"');
                printer.Append(str, start, count);
                if (wrap) printer.Append('"');
            }

            for (int ib = 0, ie; (ie = cmd.IndexOf('\n', ib)) >= 0; ib = ie + 1)
            {
                // First line only contains both the program name and the first switch.
                // We can rely on at least the '--out_dir' switch being always present.
                if (ib == 0)
                {
                    int iep = cmd.IndexOf(" --");
                    if (iep > 0)
                    {
                        PrintQuoting(cmd, 0, iep);
                        ib = iep + 1;
                    }
                }
                printer.Append(' ');
                if (cmd[ib] == '-')
                {
                    // Print switch unquoted, including '=' if any.
                    int iarg = cmd.IndexOf('=', ib, ie - ib);
                    if (iarg < 0)
                    {
                        // Bare switch without a '='.
                        printer.Append(cmd, ib, ie - ib);
                        continue;
                    }
                    printer.Append(cmd, ib, iarg + 1 - ib);
                    ib = iarg + 1;
                }
                // A positional argument or switch value.
                PrintQuoting(cmd, ib, ie - ib);
            }

            base.LogToolCommand(printer.ToString());
        }

        protected override void LogEventsFromTextOutput(string singleLine, MessageImportance messageImportance)
        {
            foreach (ErrorListFilter filter in s_errorListFilters)
            {
                Match match = filter.Pattern.Match(singleLine);

                if (match.Success)
                {
                    filter.LogAction(Log, match);
                    return;
                }
            }

            base.LogEventsFromTextOutput(singleLine, messageImportance);
        }

        // Main task entry point.
        public override bool Execute()
        {
            base.UseCommandProcessor = false;

            bool ok = base.Execute();
            if (!ok)
            {
                return false;
            }

            // Read dependency output file from the compiler to retrieve the
            // definitive list of created files. Report the dependency file
            // itself as having been written to.
            if (DependencyOut != null)
            {
                string[] outputs = DepFileUtil.ReadDependencyOutputs(DependencyOut, Log);
                if (HasLoggedErrors)
                {
                    return false;
                }

                GeneratedFiles = new ITaskItem[outputs.Length];
                for (int i = 0; i < outputs.Length; i++)
                {
                    GeneratedFiles[i] = new TaskItem(outputs[i]);
                }
                AdditionalFileWrites = new ITaskItem[] { new TaskItem(DependencyOut) };
            }

            return true;
        }

        class ErrorListFilter
        {
            public Regex Pattern { get; set; }
            public Action<TaskLoggingHelper, Match> LogAction { get; set; }
        }
    };
}
