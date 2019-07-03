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

import static junit.framework.Assert.assertTrue;

import android.content.Context;
import android.support.test.InstrumentationRegistry;
import android.support.test.runner.AndroidJUnit4;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class InteropTest {
  private String host;
  private int port;
  private boolean useTls;

  @Before
  public void setUp() throws Exception {
    host =
        InstrumentationRegistry.getArguments()
            .getString("server_host", "grpc-test.sandbox.googleapis.com");
    port = Integer.parseInt(InstrumentationRegistry.getArguments().getString("server_port", "443"));
    useTls =
        Boolean.parseBoolean(InstrumentationRegistry.getArguments().getString("use_tls", "true"));

    if (useTls) {
      Context ctx = InstrumentationRegistry.getTargetContext();
      String sslRootsFile = "roots.pem";
      InputStream in = ctx.getAssets().open(sslRootsFile);
      File outFile = new File(ctx.getExternalFilesDir(null), sslRootsFile);
      OutputStream out = new FileOutputStream(outFile);
      byte[] buffer = new byte[1024];
      int bytesRead;
      while ((bytesRead = in.read(buffer)) != -1) {
        out.write(buffer, 0, bytesRead);
      }
      in.close();
      out.close();
      InteropActivity.configureSslRoots(outFile.getCanonicalPath());
    }
  }

  @Test
  public void emptyUnary() {
    assertTrue(InteropActivity.doEmpty(host, port, useTls));
  }

  @Test
  public void largeUnary() {
    assertTrue(InteropActivity.doLargeUnary(host, port, useTls));
  }

  @Test
  public void emptyStream() {
    assertTrue(InteropActivity.doEmptyStream(host, port, useTls));
  }

  @Test
  public void requestStreaming() {
    assertTrue(InteropActivity.doRequestStreaming(host, port, useTls));
  }

  @Test
  public void responseStreaming() {
    assertTrue(InteropActivity.doResponseStreaming(host, port, useTls));
  }

  @Test
  public void pingPong() {
    assertTrue(InteropActivity.doPingPong(host, port, useTls));
  }
}
