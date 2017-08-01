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
using System.Threading;
using Grpc.Core.Internal;

namespace Grpc.Core.Profiling
{
    internal struct ProfilerEntry
    {
        public enum Type
        {
            BEGIN,
            END,
            MARK
        }

        public ProfilerEntry(Timespec timespec, Type type, string tag)
        {
            this.timespec = timespec;
            this.type = type;
            this.tag = tag;
        }

        public Timespec timespec;
        public Type type;
        public string tag;

        public override string ToString()
        {
            // mimic the output format used by C core.
            return string.Format(
                "{{\"t\": {0}.{1}, \"thd\":\"unknown\", \"type\": \"{2}\", \"tag\": \"{3}\", " +
                "\"file\": \"unknown\", \"line\": 0, \"imp\": 0}}",
                timespec.TimevalSeconds, timespec.TimevalNanos.ToString("D9"),
                GetTypeAbbreviation(type), tag);
        }

        internal static string GetTypeAbbreviation(Type type)
        {
            switch (type)
            {
                case Type.BEGIN:
                    return "{";

                case Type.END:
                    return "}";
                
                case Type.MARK:
                    return ".";
                default:
                    throw new ArgumentException("Unknown type");
            }
        }
    }
}
