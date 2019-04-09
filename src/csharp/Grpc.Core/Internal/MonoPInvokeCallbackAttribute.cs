#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Use this attribute to mark methods that will be called back from P/Invoke calls.
    /// iOS (and probably other AOT platforms) needs to have delegates registered.
    /// Instead of depending on Xamarin.iOS for this, we can just create our own,
    /// the iOS runtime just checks for the type name.
    /// See: https://docs.microsoft.com/en-gb/xamarin/ios/internals/limitations#reverse-callbacks
    /// </summary>
    [AttributeUsage(AttributeTargets.Method)]
    internal sealed class MonoPInvokeCallbackAttribute : Attribute
    {
        public MonoPInvokeCallbackAttribute(Type type)
        {
            Type = type;
        }

        public Type Type { get; private set; }
    }
}
