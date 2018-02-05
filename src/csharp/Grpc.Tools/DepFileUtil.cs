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

namespace Grpc.Tools {
  internal static class DepFileUtil {
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

    // Read file names from the dependency file to the right of ':'.
    public static string[] ReadDependencyInputs(string protoDepDir, string proto,
                                                TaskLoggingHelper log) {
      string depFilename = GetDepFilenameForProto(protoDepDir, proto);
      string[] lines = ReadDepFileLines(depFilename, false, log);
      if (lines.Length == 0) {
        return lines;
      }

      var result = new List<string>();
      bool skip = true;
      foreach (string line in lines) {
        // Start at the only line separating dependency outputs from inputs.
        int ix = skip ? FindLineSeparator(line) : -1;
        skip = skip && ix < 0;
        if (skip) continue;
        string file = ExtractFilenameFromLine(line, ix + 1, line.Length);
        if (file == "") {
          log.LogMessage(MessageImportance.Low,
    $"Skipping unparsable dependency file {depFilename}.\nLine with error: '{line}'");
          return new string[0];
        }

        // Do not bend over backwards trying not to include a proto into its
        // own list of dependencies. Since a file is not older than self,
        // it is safe to add; this is purely a memory optimization.
        if (file != proto) {
          result.Add(file);
        }
      }
      return result.ToArray();
    }

    // Read file names from the dependency file to the left of ':'.
    public static string[] ReadDependencyOutputs(string depFilename,
                                                TaskLoggingHelper log) {
      string[] lines = ReadDepFileLines(depFilename, true, log);
      if (lines.Length == 0) {
        return lines;
      }

      var result = new List<string>();
      foreach (string line in lines) {
        int ix = FindLineSeparator(line);
        string file = ExtractFilenameFromLine(line, 0, ix >= 0 ? ix : line.Length);
        if (file == "") {
          log.LogError("Unable to parse generated dependency file {0}.\n" +
                       "Line with error: '{1}'", depFilename, line);
          return new string[0];
        }
        result.Add(file);

        // If this is the line with the separator, do not read further.
        if (ix >= 0)
          break;
      }
      return result.ToArray();
    }

    // Get complete dependency file name from directory hash and file name,
    // tucked onto protoDepDir, e. g.
    // ("out", "foo/file.proto") => "out/deadbeef12345678_file.protodep".
    // This way, the filenames are unique but still possible to make sense of.
    public static string GetDepFilenameForProto(string protoDepDir, string proto) {
      string dirname = Path.GetDirectoryName(proto);
      if (Platform.IsFsCaseInsensitive) {
        dirname = dirname.ToLowerInvariant();
      }
      string dirhash = HashString64Hex(dirname);
      string filename = Path.GetFileNameWithoutExtension(proto);
      return Path.Combine(protoDepDir, $"{dirhash}_{filename}.protodep");
    }

    // Get a 64-bit hash for a directory string. We treat it as if it were
    // unique, since there are not so many distinct proto paths in a project.
    // We take the first 64 bit of the string SHA1.
    // Internal for tests access only.
    internal static string HashString64Hex(string str) {
      using (var sha1 = System.Security.Cryptography.SHA1.Create()) {
        byte[] hash = sha1.ComputeHash(Encoding.UTF8.GetBytes(str));
        var hashstr = new StringBuilder(16);
        for (int i = 0; i < 8; i++) {
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
    static string ExtractFilenameFromLine(string line, int beg, int end) {
      while (beg < end && char.IsWhiteSpace(line[beg])) beg++;
      if (beg < end && end == line.Length && line[end - 1] == '\\') end--;
      while (beg < end && char.IsWhiteSpace(line[end - 1])) end--;
      if (beg == end) return "";

      string filename = line.Substring(beg, end - beg);
      try {
        // Normalize file name.
        return Path.Combine(
          Path.GetDirectoryName(filename),
          Path.GetFileName(filename));
      } catch (Exception ex) when (Exceptions.IsIoRelated(ex)) {
        return "";
      }
    }

    // Finds the index of the ':' separating dependency clauses in the line,
    // not taking Windows drive spec into account. Returns the index of the
    // separating ':', or -1 if no separator found.
    static int FindLineSeparator(string line) {
      // Mind this case where the first ':' is not separator:
      // C:\foo\bar\.pb.h: C:/protobuf/wrappers.proto\
      int ix = line.IndexOf(':');
      if (ix <= 0 || ix == line.Length - 1
          || (line[ix + 1] != '/' && line[ix + 1] != '\\')
          || !char.IsLetter(line[ix - 1]))
        return ix;  // Not a windows drive: no letter before ':', or no '\' after.
      for (int j = ix - 1; --j >= 0;) {
        if (!char.IsWhiteSpace(line[j]))
          return ix;  // Not space or BOL only before "X:/".
      }
      return line.IndexOf(':', ix + 1);
    }

    // Read entire dependency file. The 'required' parameter controls error
    // logging behavior in case the file not found. We require this file when
    // compiling, but reading it is optional when computing depnedencies.
    static string[] ReadDepFileLines(string filename, bool required,
                                     TaskLoggingHelper log) {
      try {
        var result = File.ReadAllLines(filename);
        if (!required)
          log.LogMessage(MessageImportance.Low, $"Using dependency file {filename}");
        return result;
      } catch (Exception ex) when (Exceptions.IsIoRelated(ex)) {
        if (required) {
          log.LogError($"Unable to load {filename}: {ex.GetType().Name}: {ex.Message}");
        } else {
          log.LogMessage(MessageImportance.Low, $"Skippping {filename}: {ex.Message}");
        }
        return new string[0];
      }
    }
  };
}
