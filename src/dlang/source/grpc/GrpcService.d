module grpc.GrpcService;



interface  GrpcService
{ 
    string getModule();
    ubyte[] process(string method , ubyte[] data);
}