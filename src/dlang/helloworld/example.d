module helloworld.example;

import helloworld.helloworld;

import helloworld.helloworldrpc;
import grpc;
import kiss.logger;
import std.stdio;

class GreeterImpl : GreeterBase
{
    override HelloReply SayHello(HelloRequest request)
    {
        HelloReply reply = new HelloReply();
        reply.message = "hello " ~ request.name;
        return reply;
    }
}


void main()
{
    string host = "0.0.0.0";
	ushort port = 50051;

	Server server = new Server();
	server.listen(host , port);
	server.register( new GreeterImpl());
	server.start();

    auto channel = new Channel("127.0.0.1" , port);
	GreeterClient client = new GreeterClient(channel);
    
    string[] test_name = ["1" , "2" , "3" , "4" , "5" , "6" , "7" , "8" , "9" , "0"];
	
    foreach(name ; test_name)	
	{
		HelloRequest request = new HelloRequest();
		request.name = name;
		HelloReply reply = client.SayHello(request);
        logError(reply.message);
    }

}