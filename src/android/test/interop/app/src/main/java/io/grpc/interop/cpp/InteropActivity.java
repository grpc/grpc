/*
 * Copyright 2018, gRPC Authors All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package io.grpc.interop.cpp;

import android.content.Context;
import android.os.AsyncTask;
import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.text.TextUtils;
import android.text.method.ScrollingMovementMethod;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import java.lang.ref.WeakReference;

public class InteropActivity extends AppCompatActivity {

  static {
    System.loadLibrary("grpc-interop");
  }

  private Button sendButton;
  private EditText hostEdit;
  private EditText portEdit;
  private TextView resultText;
  private GrpcTask grpcTask;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_interop);
    sendButton = (Button) findViewById(R.id.ping_pong_button);
    hostEdit = (EditText) findViewById(R.id.host_edit_text);
    portEdit = (EditText) findViewById(R.id.port_edit_text);
    resultText = (TextView) findViewById(R.id.grpc_result_text);
    resultText.setMovementMethod(new ScrollingMovementMethod());
  }

  @Override
  protected void onPause() {
    super.onPause();
    if (grpcTask != null) {
      grpcTask.cancel(true);
      grpcTask = null;
    }
  }

  public void doPingPong(View view) {
    ((InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE))
        .hideSoftInputFromWindow(hostEdit.getWindowToken(), 0);
    sendButton.setEnabled(false);
    resultText.setText("");
    grpcTask = new GrpcTask(this);
    grpcTask.executeOnExecutor(
        AsyncTask.THREAD_POOL_EXECUTOR,
        hostEdit.getText().toString(),
        portEdit.getText().toString());
  }

  private static class GrpcTask extends AsyncTask<String, Void, String> {
    private final WeakReference<InteropActivity> activityReference;

    private GrpcTask(InteropActivity activity) {
      this.activityReference = new WeakReference<InteropActivity>(activity);
    }

    @Override
    protected String doInBackground(String... params) {
      String host = params[0];
      String portStr = params[1];
      int port = TextUtils.isEmpty(portStr) ? 50051 : Integer.valueOf(portStr);
      // TODO(ericgribkoff) Support other test cases in the app UI
      if (doPingPong(host, port, false)) {
        return "Success";
      } else {
        return "Failure";
      }
    }

    @Override
    protected void onPostExecute(String result) {
      InteropActivity activity = activityReference.get();
      if (activity == null || isCancelled()) {
        return;
      }
      TextView resultText = (TextView) activity.findViewById(R.id.grpc_result_text);
      Button sendButton = (Button) activity.findViewById(R.id.ping_pong_button);
      resultText.setText(result);
      sendButton.setEnabled(true);
    }
  }

  public static native boolean doEmpty(String host, int port, boolean useTls);

  public static native boolean doLargeUnary(String host, int port, boolean useTls);

  public static native boolean doEmptyStream(String host, int port, boolean useTls);

  public static native boolean doRequestStreaming(String host, int port, boolean useTls);

  public static native boolean doResponseStreaming(String host, int port, boolean useTls);

  public static native boolean doPingPong(String host, int port, boolean useTls);
}
