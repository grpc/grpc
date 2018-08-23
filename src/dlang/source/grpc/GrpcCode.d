module grpc.GrpcCode;

string GetFunc(string funcstr)
{
    import std.string;
    string[] funcs = funcstr.split(".");
    string myFunc;
    if (funcs.length > 0)
        myFunc = funcs[$ - 1];
    else
        myFunc = funcstr;
    return myFunc;
}

string CM(O , string service , string funcs = __FUNCTION__)()
{
    string func = GetFunc(funcs);
    string code = 
    `auto data = cast(ubyte[])_channel.send("/`~ service ~`/`~func~`" ,request.toProtobuf.array);
	auto reply = new `~ O.stringof ~`();
	data.fromProtobuf!`~ O.stringof ~`(reply);
	return reply;`;
    return code;
}

string CMA( O  , string service , string funcs = __FUNCTION__)()
{
    string func = GetFunc(funcs);
    string code = `
    _channel.sendAsync("`~service~`/`~func~`" , request.toProtobuf.array ,
		(Result!(ubyte[]) data){
			Result!`~ O.stringof ~` result;
			if(data.failed)
			{
				result = new Result!`~O.stringof~`(data.cause());
			}
			else
			{
				auto reply = new `~O.stringof~`();
				auto udata = data.result;
				try{
					udata.fromProtobuf!`~O.stringof~`(reply);
				}
				catch(Throwable e)
				{
					result = new Result!`~O.stringof~`(new GrpcDataErrorException(e.msg));
				}
				if(result is null)
					result = new Result!`~O.stringof~`(reply);
			}
			dele(result);
		});`;
    return code;
}

string SM(I ,string method , string funcs = __FUNCTION__)()
{
    string code = `case "`~method~`":
				auto request = new `~I.stringof~`();
				data.fromProtobuf!`~I.stringof~`(request);
				auto reply = `~method~`(request);
				return reply.toProtobuf.array;`;
    return code;
}
