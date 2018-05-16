#region Copyright notice and license

// Copyright 2016 gRPC authors.
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
using Grpc.Core;
using Grpc.Testing;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Helpers for Control.cs
    /// </summary>
    public static class ControlExtensions
    {
        public static ChannelOption ToChannelOption(this ChannelArg channelArgument)
        {
            switch (channelArgument.ValueCase)
            {
                case ChannelArg.ValueOneofCase.StrValue:
                  return new ChannelOption(channelArgument.Name, channelArgument.StrValue);
                case ChannelArg.ValueOneofCase.IntValue:
                  return new ChannelOption(channelArgument.Name, channelArgument.IntValue);
                default:
                  throw new ArgumentException("Unsupported channel argument value.");
            }
        }
    }
}
