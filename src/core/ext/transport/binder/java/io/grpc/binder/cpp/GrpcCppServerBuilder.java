// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package io.grpc.binder.cpp;

import android.os.IBinder;
import android.util.Log;

/* EXPERIMENTAL. Provides a interface to get endpoint binder from C++ */
public class GrpcCppServerBuilder {
  private static final String logTag = "GrpcCppServerBuilder";

  public static IBinder GetEndpointBinder(String uri) {
    String scheme = "binder:";
    if (uri.startsWith(scheme)) {
      String path = uri.substring(scheme.length());
      // TODO(mingcl): Consider if we would like to make sure the path only contain valid
      // characters here
      IBinder ibinder = GetEndpointBinderInternal(path);
      Log.e(logTag, "Returning binder=" + ibinder + " for URI=" + uri);
      return ibinder;
    } else {
      Log.e(logTag, "URI " + uri + " does not start with 'binder:'");
      return null;
    }
  }

  private static native IBinder GetEndpointBinderInternal(String conn_id);
}
