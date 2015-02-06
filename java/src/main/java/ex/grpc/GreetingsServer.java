package ex.grpc;

import com.google.common.util.concurrent.MoreExecutors;
import com.google.net.stubby.ServerImpl;
import com.google.net.stubby.transport.netty.NettyServerBuilder;

import java.util.concurrent.TimeUnit;

/**
 * Server that manages startup/shutdown of a {@code Greetings} server.
 */
public class GreetingsServer {
  /* The port on which the server should run */
  private int port = 50051;
  private ServerImpl server;

  private void start() throws Exception {
    server = NettyServerBuilder.forPort(port)
             .addService(GreetingsGrpc.bindService(new GreetingsImpl()))
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
    final GreetingsServer server = new GreetingsServer();

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
