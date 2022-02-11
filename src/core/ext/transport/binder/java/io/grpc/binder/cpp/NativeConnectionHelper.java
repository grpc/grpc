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
import android.content.pm.PackageManager;
import android.os.Parcel;
import android.util.Log;
import java.util.HashMap;
import java.util.Map;

/**
 * This class will be invoked by gRPC binder transport internal implementation (from
 * src/core/ext/transport/binder/client/jni_utils.cc) to perform operations that are only possible
 * in Java
 */
final class NativeConnectionHelper {
  // Maps connection id to GrpcBinderConnection instances
  static Map<String, GrpcBinderConnection> s = new HashMap<>();

  static void tryEstablishConnection(Context context, String pkg, String cls, String action_name, String connId) {
    // TODO(mingcl): Assert that connId is unique
    s.put(connId, new GrpcBinderConnection(context, connId));
    s.get(connId).tryConnect(pkg, cls, action_name);
  }

  // Returns true if the packages signature of the 2 UIDs match.
  // `context` is used to get PackageManager.
  // Suppress unnecessary internal warnings related to checkSignatures compatibility issue.
  // BinderTransport code is only used on newer Android platform versions so this is fine.
  @SuppressWarnings("CheckSignatures")
  static boolean isSignatureMatch(Context context, int uid1, int uid2) {
    int result = context.getPackageManager().checkSignatures(uid1, uid2);
    if (result == PackageManager.SIGNATURE_MATCH) {
      return true;
    }
    Log.e(
        "NativeConnectionHelper",
        "Signatures does not match. checkSignature return value = " + result);
    return false;
  }

  static Parcel getEmptyParcel() {
    return Parcel.obtain();
  }
}
