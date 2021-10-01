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
import android.os.IBinder;
import android.os.Parcel;
import java.util.Map;
import java.util.HashMap;

/**
 * This class will be invoked by gRPC binder transport internal implementation to perform operations
 * that are only possible in Java
 */
final class NativeConnectionHelper {
  static Map<String, SyncServiceConnection> s = new HashMap<String, SyncServiceConnection>();

  static void tryEstablishConnection(Context context, String pkg, String cls, String conn_id) {
    // TODO(mingcl): Assert that conn_id is unique
    s.put(conn_id, new SyncServiceConnection(context, conn_id));
    s.get(conn_id).tryConnect(pkg, cls);
  }

  static IBinder getServiceBinder(String conn_id) {
    // TODO(mingcl): Checks conn_id exists
    return s.get(conn_id).getIBinder();
  }

  static Parcel getEmptyParcel() {
    return Parcel.obtain();
  }
}
