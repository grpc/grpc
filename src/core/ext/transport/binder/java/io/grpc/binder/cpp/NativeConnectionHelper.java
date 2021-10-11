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

/**
 * This class will be invoked by gRPC binder transport internal implementation to perform operations
 * that are only possible in Java
 */
final class NativeConnectionHelper {
  static SyncServiceConnection s;

  static void tryEstablishConnection(Context context, String pkg, String cls) {
    s = new SyncServiceConnection(context);
    s.tryConnect(pkg, cls);
  }

  // TODO(mingcl): We should notify C++ once we got the service binder so they don't need to call
  // this function to check. For now we assume that this function will only be called after
  // successful connection
  static IBinder getServiceBinder() {
    return s.getIBinder();
  }

  static Parcel getEmptyParcel() {
    return Parcel.obtain();
  }
}
