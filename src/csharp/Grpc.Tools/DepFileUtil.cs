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
using System.IO;
using System.Text;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Grpc.Tools
{
    internal static class DepFileUtil
    {
        /*
           Sample dependency files. Notable features we have to deal with:
            * Slash doubling, must normalize them.
            * Spaces in file names. Cannot just "unwrap" the line on backslash at eof;
              rather, treat every line as containing one file name except for one with
              the ':' separator, as containing exactly two.
            * Deal with ':' also being drive letter separator (second example).

        obj\Release\net45\/Foo.cs \
        obj\Release\net45\/FooGrpc.cs: C:/foo/include/google/protobuf/wrappers.proto\
         C:/projects/foo/src//foo.proto

        C:\projects\foo\src\./foo.grpc.pb.cc \
        C:\projects\foo\src\./foo.grpc.pb.h \
        C:\projects\foo\src\./foo.pb.cc \
        C:\projects\foo\src\./foo.pb.h: C:/foo/include/google/protobuf/wrappers.proto\
         C:/foo/include/google/protobuf/any.proto\
         C:/foo/include/google/protobuf/source_context.proto\
         C:/foo/include/google/protobuf/type.proto\
         foo.proto
        */

        /// <summary>
        /// Read file names from the dependency file to the right of ':'
        /// </summary>
        /// <param name="protoDepDir">Relative path to the dependency cache, e. g. "out"</param>
        /// <param name="proto">Relative path to the proto item, e. g. "foo/file.proto"</param>
        /// <param name="log">A <see cref="TaskLoggingHelper"/> for logging</param>
        /// <returns>
        /// Array of the proto file <b>input</b> dependencies as written by protoc, or empty
        /// array if the dependency file does not exist or cannot be parsed.
        /// </returns>
        public static string[] ReadDependencyInputs(string protoDepDir, string proto,
                                                    TaskLoggingHelper log)
        {
            string depFilename = GetDepFilenameForProto(protoDepDir, proto);
            string[] lines = ReadDepFileLines(depFilename, false, log);
            if (lines.Length == 0)
            {
                return lines;
            }

            var result = new List<string>();
            bool skip = true;
            foreach (string line in lines)
            {
                // Start at the only line separating dependency outputs from inputs.
                int ix = skip ? FindLineSeparator(line) : -1;
                skip = skip && ix < 0;
                if (skip) { continue; }
                string file = ExtractFilenameFromLine(line, ix + 1, line.Length);
                if (file == "")
                {
                    log.LogMessage(MessageImportance.Low,
              $"Skipping unparsable dependency file {depFilename}.\nLine with error: '{line}'");
                    return new string[0];
                }

                // Do not bend over backwards trying not to include a proto into its
                // own list of dependencies. Since a file is not older than self,
                // it is safe to add; this is purely a memory optimization.
                if (file != proto)
                {
                    result.Add(file);
                }
            }
            return result.ToArray();
        }

        /// <summary>
        /// Read file names from the dependency file to the left of ':'
        /// </summary>
        /// <param name="depFilename">Path to dependency file written by protoc</param>
        /// <param name="log">A <see cref="TaskLoggingHelper"/> for logging</param>
        /// <returns>
        /// Array of the protoc-generated outputs from the given dependency file
        /// written by protoc, or empty array if the file does not exist or cannot
        /// be parsed.
        /// </returns>
        /// <remarks>
        /// Since this is called after a protoc invocation, an unparsable or missing
        /// file causes an error-level message to be logged.
        /// </remarks>
        public static string[] ReadDependencyOutputs(string depFilename,
                                                    TaskLoggingHelper log)
        {
            string[] lines = ReadDepFileLines(depFilename, true, log);
            if (lines.Length == 0)
            {
                return lines;
            }

            var result = new List<string>();
            foreach (string line in lines)
            {
                int ix = FindLineSeparator(line);
                string file = ExtractFilenameFromLine(line, 0, ix >= 0 ? ix : line.Length);
                if (file == "")
                {
                    log.LogError("Unable to parse generated dependency file {0}.\n" +
                                 "Line with error: '{1}'", depFilename, line);
                    return new string[0];
                }
                result.Add(file);

                // If this is the line with the separator, do not read further.
                if (ix >= 0) { break; }
            }
            return result.ToArray();
        }

        /// <summary>
        /// Construct relative dependency file name from directory hash and file name
        /// </summary>
        /// <param name="protoDepDir">Relative path to the dependency cache, e. g. "out"</param>
        /// <param name="proto">Relative path to the proto item, e. g. "foo/file.proto"</param>
        /// <returns>
        /// Full relative path to the dependency file, e. g.
        /// "out/deadbeef12345678_file.protodep"
        /// </returns>
        /// <remarks>
        /// See <see cref="GetDirectoryHash"/> for notes on directory hash.
        /// </remarks>
        public static string GetDepFilenameForProto(string protoDepDir, string proto)
        {
            string dirhash = GetDirectoryHash(proto);
            string filename = Path.GetFileNameWithoutExtension(proto);
            return Path.Combine(protoDepDir, $"{dirhash}_{filename}.protodep");
        }

        /// <summary>
        /// Construct relative output directory with directory hash
        /// </summary>
        /// <param name="outputDir">Relative path to the output directory, e. g. "out"</param>
        /// <param name="proto">Relative path to the proto item, e. g. "foo/file.proto"</param>
        /// <returns>
        /// Full relative path to the directory, e. g. "out/deadbeef12345678"
        /// </returns>
        /// <remarks>
        /// See <see cref="GetDirectoryHash"/> for notes on directory hash.
        /// </remarks>
        public static string GetOutputDirWithHash(string outputDir, string proto)
        {
            string dirhash = GetDirectoryHash(proto);
            return Path.Combine(outputDir, dirhash);
        }

        /// <summary>
        /// Construct the directory hash from a relative file name
        /// </summary>
        /// <param name="proto">Relative path to the proto item, e. g. "foo/file.proto"</param>
        /// <returns>
        /// Directory hash based on the file name, e. g. "deadbeef12345678"
        /// </returns>
        /// <remarks>
        /// Since a project may contain proto files with the same filename but in different
        /// directories, a unique directory for the generated files is constructed based on the
        /// proto file names directory. The directory path can be arbitrary, for example,
        /// it can be outside of the project, or an absolute path including a drive letter,
        /// or a UNC network path. A name constructed from such a path by, for example,
        /// replacing disallowed name characters with an underscore, may well be over
        /// filesystem's allowed path length, since it will be located under the project
        /// and solution directories, which are also some level deep from the root.
        /// Instead of creating long and unwieldy names for these proto sources, we cache
        /// the full path of the name without the filename, as in e. g. "foo/file.proto"
        /// will yield the name "deadbeef12345678", where that is a presumed hash value
        /// of the string "foo". This allows the path to be short, unique (up to a hash
        /// collision), and still allowing the user to guess their provenance.
        /// </remarks>
        private static string GetDirectoryHash(string proto)
        {
            string dirname = Path.GetDirectoryName(proto);
            if (Platform.IsFsCaseInsensitive)
            {
                dirname = dirname.ToLowerInvariant();
            }

            return HashString64Hex(dirname);
        }

        // Get a 64-bit hash for a directory string. We treat it as if it were
        // unique, since there are not so many distinct proto paths in a project.
        // We take the first 64 bit of the string SHA1.
        // Internal for tests access only.
        internal static string HashString64Hex(string str)
        {
            using (var sha1 = System.Security.Cryptography.SHA1.Create())
            {
                byte[] hash = sha1.ComputeHash(Encoding.UTF8.GetBytes(str));
                var hashstr = new StringBuilder(16);
                for (int i = 0; i < 8; i++)
                {
                    hashstr.Append(hash[i].ToString("x2"));
                }
                return hashstr.ToString();
            }
        }

        // Extract filename between 'beg' (inclusive) and 'end' (exclusive) from
        // line 'line', skipping over trailing and leading whitespace, and, when
        // 'end' is immediately past end of line 'line', also final '\' (used
        // as a line continuation token in the dep file).
        // Returns an empty string if the filename cannot be extracted.
        static string ExtractFilenameFromLine(string line, int beg, int end)
        {
            while (beg < end && char.IsWhiteSpace(line[beg])) beg++;
            if (beg < end && end == line.Length && line[end - 1] == '\\') end--;
            while (beg < end && char.IsWhiteSpace(line[end - 1])) end--;
            if (beg == end) return "";

            string filename = line.Substring(beg, end - beg);
            try
            {
                // Normalize file name.
                return Path.Combine(Path.GetDirectoryName(filename), Path.GetFileName(filename));
            }
            catch (Exception ex) when (Exceptions.IsIoRelated(ex))
            {
                return "";
            }
        }

        // Finds the index of the ':' separating dependency clauses in the line,
        // not taking Windows drive spec into account. Returns the index of the
        // separating ':', or -1 if no separator found.
        static int FindLineSeparator(string line)
        {
            // Mind this case where the first ':' is not separator:
            // C:\foo\bar\.pb.h: C:/protobuf/wrappers.proto\
            int ix = line.IndexOf(':');
            if (ix <= 0 || ix == line.Length - 1
                || (line[ix + 1] != '/' && line[ix + 1] != '\\')
                || !char.IsLetter(line[ix - 1]))
            {
                return ix;  // Not a windows drive: no letter before ':', or no '\' after.
            }
            for (int j = ix - 1; --j >= 0;)
            {
                if (!char.IsWhiteSpace(line[j]))
                {
                    return ix;  // Not space or BOL only before "X:/".
                }
            }
            return line.IndexOf(':', ix + 1);
        }

        // Read entire dependency file. The 'required' parameter controls error
        // logging behavior in case the file not found. We require this file when
        // compiling, but reading it is optional when computing dependencies.
        static string[] ReadDepFileLines(string filename, bool required,
                                         TaskLoggingHelper log)
        {
            try
            {
                var result = File.ReadAllLines(filename);
                if (!required)
                {
                    log.LogMessage(MessageImportance.Low, $"Using dependency file {filename}");
                }
                return result;
            }
            catch (Exception ex) when (Exceptions.IsIoRelated(ex))
            {
                if (required)
                {
                    log.LogError($"Unable to load {filename}: {ex.GetType().Name}: {ex.Message}");
                }
                else
                {
                    log.LogMessage(MessageImportance.Low, $"Skipping {filename}: {ex.Message}");
                }
                return new string[0];
            }
        }
    };
}
