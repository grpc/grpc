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

package io.grpc.helloworldexample.cpp;

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
import android.widget.Toast;
import java.lang.ref.WeakReference;

public class HelloworldActivity extends AppCompatActivity {

  static {
    System.loadLibrary("grpc-helloworld");
  }

  private Button sendButton;
  private Button serverButton;
  private EditText hostEdit;
  private EditText portEdit;
  private EditText messageEdit;
  private EditText serverPortEdit;
  private TextView resultText;
  private GrpcTask grpcTask;
  private RunServerTask runServerTask;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_helloworld);
    sendButton = (Button) findViewById(R.id.send_button);
    serverButton = (Button) findViewById(R.id.server_button);
    hostEdit = (EditText) findViewById(R.id.host_edit_text);
    portEdit = (EditText) findViewById(R.id.port_edit_text);
    messageEdit = (EditText) findViewById(R.id.message_edit_text);
    serverPortEdit = (EditText) findViewById(R.id.server_port_edit_text);
    resultText = (TextView) findViewById(R.id.grpc_response_text);
    resultText.setMovementMethod(new ScrollingMovementMethod());
  }

  @Override
  protected void onPause() {
    super.onPause();
    if (runServerTask != null) {
      runServerTask.cancel(true);
      runServerTask = null;
      serverButton.setText("Start gRPC Server");
    }
    if (grpcTask != null) {
      grpcTask.cancel(true);
      grpcTask = null;
    }
  }

  public void sendMessage(View view) {
    ((InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE))
        .hideSoftInputFromWindow(hostEdit.getWindowToken(), 0);
    sendButton.setEnabled(false);
    resultText.setText("");
    grpcTask = new GrpcTask(this);
    grpcTask.executeOnExecutor(
        AsyncTask.THREAD_POOL_EXECUTOR,
        hostEdit.getText().toString(),
        messageEdit.getText().toString(),
        portEdit.getText().toString());
  }

  public void startOrStopServer(View view) {
    if (runServerTask != null) {
      runServerTask.cancel(true);
      runServerTask = null;
      serverButton.setText("Start gRPC Server");
      Toast.makeText(this, "Server stopped", Toast.LENGTH_SHORT).show();
    } else {
      runServerTask = new RunServerTask(this);
      String portStr = serverPortEdit.getText().toString();
      int port = TextUtils.isEmpty(portStr) ? 50051 : Integer.valueOf(portStr);
      runServerTask.executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR, port);
      serverButton.setText("Stop gRPC Server");
      Toast.makeText(this, "Server started on port " + port, Toast.LENGTH_SHORT).show();
    }
  }

  private static class RunServerTask extends AsyncTask<Integer, Void, Void> {
    private final WeakReference<HelloworldActivity> activityReference;

    private RunServerTask(HelloworldActivity activity) {
      this.activityReference = new WeakReference<HelloworldActivity>(activity);
    }

    @Override
    protected Void doInBackground(Integer... params) {
      int port = params[0];
      HelloworldActivity activity = activityReference.get();
      if (activity != null) {
        activity.startServer(port);
      }
      return null;
    }
  }

  private static class GrpcTask extends AsyncTask<String, Void, String> {
    private final WeakReference<HelloworldActivity> activityReference;

    private GrpcTask(HelloworldActivity activity) {
      this.activityReference = new WeakReference<HelloworldActivity>(activity);
    }

    @Override
    protected String doInBackground(String... params) {
      String host = params[0];
      String message = params[1];
      String portStr = params[2];
      int port = TextUtils.isEmpty(portStr) ? 50051 : Integer.valueOf(portStr);
      return sayHello(host, port, message);
    }

    @Override
    protected void onPostExecute(String result) {
      HelloworldActivity activity = activityReference.get();
      if (activity == null || isCancelled()) {
        return;
      }
      TextView resultText = (TextView) activity.findViewById(R.id.grpc_response_text);
      Button sendButton = (Button) activity.findViewById(R.id.send_button);
      resultText.setText(result);
      sendButton.setEnabled(true);
    }
  }

  /**
   * Invoked by native code to stop server when RunServerTask has been canceled, either by user
   * request or upon app moving to background.
   */
  public boolean isRunServerTaskCancelled() {
    if (runServerTask != null) {
      return runServerTask.isCancelled();
    }
    return false;
  }

  public static native String sayHello(String host, int port, String message);

  public native void startServer(int port);
}
