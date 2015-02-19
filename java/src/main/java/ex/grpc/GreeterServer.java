package ex.grpc;

import com.google.common.util.concurrent.MoreExecutors;
import com.google.net.stubby.ServerImpl;
import com.google.net.stubby.transport.netty.NettyServerBuilder;

import java.util.concurrent.TimeUnit;

/**
 * Server that manages startup/shutdown of a {@code Greeter} server.
 */
public class GreeterServer {
  /* The port on which the server should run */
  private int port = 50051;
  private ServerImpl server;

  private void start() throws Exception {
    server = NettyServerBuilder.forPort(port)
             .addService(GreeterGrpc.bindService(new GreeterImpl()))
             .build();
    server.startAsync();
    server.awaitRunning(5, TimeUnit.SECONDS);
    System.out.println("Server started on port:" + port);
  }

  private void stop() throws Exception {
    server.stopAsync();
    server.awaitTerminated();
    System.out.println("Server shutting down ...");
  }

  /**
   * Main launches the server from the command line.
   */
  public static void main(String[] args) throws Exception {
    final GreeterServer server = new GreeterServer();

    Runtime.getRuntime().addShutdownHook(new Thread() {
      @Override
      public void run() {
        try {
          System.out.println("Shutting down");
          server.stop();
        } catch (Exception e) {
          e.printStackTrace();
        }
      }
      });
    server.start();
  }
}
