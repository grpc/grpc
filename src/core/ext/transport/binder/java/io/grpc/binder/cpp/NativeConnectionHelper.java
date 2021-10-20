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

import android.content.Context;
import android.os.Parcel;
import java.util.HashMap;
import java.util.Map;

/**
 * This class will be invoked by gRPC binder transport internal implementation to perform operations
 * that are only possible in Java
 */
final class NativeConnectionHelper {
  // Maps connection id to GrpcBinderConnection instances
  static Map<String, GrpcBinderConnection> s = new HashMap<>();

  static void tryEstablishConnection(Context context, String pkg, String cls, String connId) {
    // TODO(mingcl): Assert that connId is unique
    s.put(connId, new GrpcBinderConnection(context, connId));
    s.get(connId).tryConnect(pkg, cls);
  }

  static Parcel getEmptyParcel() {
    return Parcel.obtain();
  }
}
