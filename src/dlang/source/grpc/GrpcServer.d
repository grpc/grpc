module grpc.GrpcServer;

import std.stdio;

import hunt.http.codec.http.frame;
import hunt.http.codec.http.model;
import hunt.http.codec.http.stream;

import hunt.http.server.http.HTTP2Server;
import hunt.http.server.http.ServerHTTPHandler;
import hunt.http.server.http.ServerSessionListener;

import std.string;

import hunt.util.functional;
import hunt.container;

import kiss.logger;
import grpc.GrpcService;






alias Server = GrpcServer;
class GrpcServer
{
    this()
    {
        	_http2Configuration = new HTTP2Configuration();
            _http2Configuration.setSecureConnectionEnabled(false);
            _http2Configuration.setFlowControlStrategy("simple");
            _http2Configuration.getTcpConfiguration().setTimeout(60 * 1000);
            _http2Configuration.setProtocol(HttpVersion.HTTP_2.asString());

            _settings = new HashMap!(int, int)();
            _settings.put(SettingsFrame.HEADER_TABLE_SIZE, _http2Configuration.getMaxDynamicTableSize());
            _settings.put(SettingsFrame.INITIAL_WINDOW_SIZE, _http2Configuration.getInitialStreamSendWindow());
    }

    void listen(string address , ushort port)
    {
        _server = new HTTP2Server(address, port, _http2Configuration, 
	    new class ServerSessionListener {

            override
            Map!(int, int) onPreface(Session session) {
                infof("server received preface: %s", session);
                return _settings;
            }

            override
            StreamListener onNewStream(Stream stream, HeadersFrame frame) {
                infof("server created new stream: %d", stream.getId());
                infof("server created new stream headers: %s", frame.getMetaData().toString());
                auto request = cast(MetaData.Request)frame.getMetaData();

                string path = request.getURI().getPath();
                auto arr = path.split("/");
                auto mod = arr[1];
                auto method = arr[2];

                auto service =  mod in _router ;
                if(service == null)
                {    
                    logError("no router " , mod , " " , path ," " , arr);
                    return null;
                }

                HttpFields fields = new HttpFields();
                fields.put("content-type" ,"application/grpc+proto");
                fields.put("grpc-accept-encoding" , "identity");
                fields.put("accept-encoding" , "identity");

                auto response = new MetaData.Response(HttpVersion.HTTP_2 , 200 , fields);
                auto res_header = new HeadersFrame(stream.getId(),response , null , false);
                HttpFields end_fileds = new HttpFields();
                end_fileds.put("grpc-status" , "0");
                auto res_end_header =
                 new HeadersFrame(stream.getId(),new MetaData(HttpVersion.HTTP_2, end_fileds), null , true);

                logWarning("normal ...");

                return new class StreamListener {

                    override
                    void onHeaders(Stream stream, HeadersFrame frame) {
                        infof("server received headers: %s", frame.getMetaData());
                    }

                    override
                    StreamListener onPush(Stream stream, PushPromiseFrame frame) {
                        return null;
                    }

                    override
                    void onData(Stream stream, DataFrame frame, Callback callback) {
                        infof("server received data %s, %s", BufferUtils.toString(frame.getData()), frame);
                        
                       
                        stream.headers(res_header , Callback.NOOP);
                        
                        auto bytes = cast(ubyte[])BufferUtils.toString(frame.getData());
                        logWarning("normal data..." , bytes);  
                        auto data = (*service).process(method , bytes[5 .. $].dup);
                        ubyte compress = 0;
        
                        import std.bitmanip;
                        ubyte[4] len = nativeToBigEndian(cast(int)data.length);
                        ubyte[] grpc_data;
                        grpc_data ~= compress;
                        grpc_data ~= len;
                        grpc_data ~= data;

                        DataFrame smallDataFrame = new DataFrame(stream.getId(),
			            ByteBuffer.wrap(cast(byte[])grpc_data), false);
                        
                        stream.data(smallDataFrame , Callback.NOOP);
                        
                        stream.headers(res_end_header , Callback.NOOP);
                        callback.succeeded();
                    }

                    void onReset(Stream stream, ResetFrame frame, Callback callback) {
                        try {
                            onReset(stream, frame);
                            callback.succeeded();
                        } catch (Exception x) {
                            callback.failed(x);
                        }
                    }

                    override
                    void onReset(Stream stream, ResetFrame frame) {
                        infof("server reseted: %s | %s", stream, frame);
                    }

                    override
                    bool onIdleTimeout(Stream stream, Exception x) {
                        infof("idle timeout", x);
                        return true;
                    }

                    override string toString() {
                        return super.toString();
                    }

                };
            }

            override
            void onSettings(Session session, SettingsFrame frame) {
                infof("server received settings: %s", frame);
            }

            override
            void onPing(Session session, PingFrame frame) {
            }

            override
            void onReset(Session session, ResetFrame frame) {
                infof("server reset " ~ frame.toString());
            }

            override
            void onClose(Session session, GoAwayFrame frame) {
                infof("server closed " ~ frame.toString());
            }

            override
            void onFailure(Session session, Exception failure) {
                errorf("server failure, %s", failure, session);
            }

            void onClose(Session session, GoAwayFrame frame, Callback callback)
            {
                try
                {
                    onClose(session, frame);
                    callback.succeeded();
                }
                catch (Exception x)
                {
                    callback.failed(x);
                }
            }

            void onFailure(Session session, Exception failure, Callback callback)
            {
                try
                {
                    onFailure(session, failure);
                    callback.succeeded();
                }
                catch (Exception x)
                {
                    callback.failed(x);
                }
            }

            override
            void onAccept(Session session) {
            }

            override
            bool onIdleTimeout(Session session) {
                return false;
            }
        }, new ServerHTTPHandlerAdapter(), null);
    }

    void register(GrpcService service)
    {
        _router[service.getModule()] = service;
    }

    void start()
    {
        _server.start();
    }

    void stop()
    {
        _server.stop();
    }

  

    protected:
        HTTP2Configuration      _http2Configuration;
        Map!(int, int)          _settings;
        HTTP2Server             _server;
        GrpcService[string]     _router;

}