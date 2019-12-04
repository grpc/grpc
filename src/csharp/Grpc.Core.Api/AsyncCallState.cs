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
using System.Threading.Tasks;

namespace Grpc.Core
{
    /// <summary>
    /// Provides an abstraction over the callback providers
    /// used by AsyncUnaryCall, AsyncDuplexStreamingCall, etc
    /// </summary>
    internal struct AsyncCallState
    {
        readonly object responseHeadersAsync; // Task<Metadata> or Func<object, Task<Metadata>>
        readonly object getStatusFunc; // Func<Status> or Func<object, Status>
        readonly object getTrailersFunc; // Func<Metadata> or Func<object, Metadata>
        readonly object disposeAction; // Action or Action<object>
        readonly object callbackState; // arg0 for the callbacks above, if needed

        internal AsyncCallState(
            Func<object, Task<Metadata>> responseHeadersAsync,
            Func<object, Status> getStatusFunc,
            Func<object, Metadata> getTrailersFunc,
            Action<object> disposeAction,
            object callbackState)
        {
            this.responseHeadersAsync = responseHeadersAsync;
            this.getStatusFunc = getStatusFunc;
            this.getTrailersFunc = getTrailersFunc;
            this.disposeAction = disposeAction;
            this.callbackState = callbackState;
        }

        internal AsyncCallState(
            Task<Metadata> responseHeadersAsync,
            Func<Status> getStatusFunc,
            Func<Metadata> getTrailersFunc,
            Action disposeAction)
        {
            this.responseHeadersAsync = responseHeadersAsync;
            this.getStatusFunc = getStatusFunc;
            this.getTrailersFunc = getTrailersFunc;
            this.disposeAction = disposeAction;
            this.callbackState = null;
        }

        internal Task<Metadata> ResponseHeadersAsync()
        {
            var withState = responseHeadersAsync as Func<object, Task<Metadata>>;
            return withState != null ? withState(callbackState)
                : (Task<Metadata>)responseHeadersAsync;
        }

        internal Status GetStatus()
        {
            var withState = getStatusFunc as Func<object, Status>;
            return withState != null ? withState(callbackState)
                : ((Func<Status>)getStatusFunc)();
        }

        internal Metadata GetTrailers()
        {
            var withState = getTrailersFunc as Func<object, Metadata>;
            return withState != null ? withState(callbackState)
                : ((Func<Metadata>)getTrailersFunc)();
        }

        internal void Dispose()
        {
            var withState = disposeAction as Action<object>;
            if (withState != null)
            {
                withState(callbackState);
            }
            else
            {
                ((Action)disposeAction)();
            }
        }
    }
}
