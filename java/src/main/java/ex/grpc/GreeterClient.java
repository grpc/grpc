package ex.grpc;

import com.google.net.stubby.ChannelImpl;
import com.google.net.stubby.stub.StreamObserver;
import com.google.net.stubby.transport.netty.NegotiationType;
import com.google.net.stubby.transport.netty.NettyChannelBuilder;

import java.util.logging.Level;
import java.util.logging.Logger;
import java.util.concurrent.TimeUnit;

public class GreeterClient {
  private final Logger logger = Logger.getLogger(
      GreeterClient.class.getName());
  private final ChannelImpl channel;
  private final GreeterGrpc.GreeterBlockingStub blockingStub;

  public GreeterClient(String host, int port) {
    channel = NettyChannelBuilder.forAddress(host, port)
              .negotiationType(NegotiationType.PLAINTEXT)
              .build();
    blockingStub = GreeterGrpc.newBlockingStub(channel);
  }

  public void shutdown() throws InterruptedException {
    channel.shutdown().awaitTerminated(5, TimeUnit.SECONDS);
  }

  public void greet(String name) {
    try {
      logger.fine("Will try to greet " + name + " ...");
      Helloworld.HelloRequest req =
          Helloworld.HelloRequest.newBuilder().setName(name).build();
      Helloworld.HelloReply reply = blockingStub.sayHello(req);
      logger.info("Greeting: " + reply.getMessage());
    } catch (RuntimeException e) {
      logger.log(Level.WARNING, "RPC failed", e);
      return;
    }
  }

  public static void main(String[] args) throws Exception {
    GreeterClient client = new GreeterClient("localhost", 50051);
    try {
      /* Access a service running on the local machine on port 50051 */
      String user = "world";
      if (args.length > 0) {
        user = args[0];  /* Use the arg as the name to greet if provided */
      }
      client.greet(user);
    } finally {
      client.shutdown();
    }
  }
}
