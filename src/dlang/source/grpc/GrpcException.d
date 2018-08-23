module grpc.GrpcException;
import std.exception;
import core.exception;


/// client exception list - will do next. (idletimeout) 
/// 1 timeout
/// 2 neterror          
/// 3 data error

///  server tips:
///  tips no module
///  tips no method
///  tips don't support protocol. 

class GrpcException: Exception
{
    ///
    this(string msg , string file = __FILE__, size_t line = __LINE__)
    {
        super(msg , file , line);
    }
}

///
class GrpcTimeoutException : GrpcException
{
    ///
    this(string msg , string file = __FILE__, size_t line = __LINE__)
    {
        super(msg , file , line);
    }
}

///
class GrpcNetErrorException : GrpcException
{

    ///
    this(string msg , string file = __FILE__, size_t line = __LINE__)
    {
        super(msg , file , line);
    }
}

///
class GrpcDataErrorException : GrpcException
{
    ///
    this(string msg , string file = __FILE__, size_t line = __LINE__)
    {
        super(msg , file , line);
    }
}