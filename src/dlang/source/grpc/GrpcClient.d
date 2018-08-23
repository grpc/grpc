module grpc.GrpcClient;

import hunt.http.client.http.SimpleHTTPClient;
import hunt.http.client.http.SimpleResponse;

import hunt.util.concurrent.Promise;
import hunt.util.concurrent.CompletableFuture;

import kiss.logger;

import std.conv;
import std.stdio;

import std.datetime;
import std.conv;
import std.stdio;

import std.stdio;

import hunt.http.client.http.ClientHTTP2SessionListener;
import hunt.http.client.http.HTTP2Client;
import hunt.http.client.http.HTTP2ClientConnection;
import hunt.http.client.http.HTTPClientConnection;

import hunt.http.codec.http.frame;
import hunt.http.codec.http.model;
import hunt.http.codec.http.stream;

import hunt.util.exception;
import hunt.util.functional;
import hunt.util.concurrent.FuturePromise;

import hunt.container;
import kiss.logger;
import std.format;
import hunt.net;

import grpc.GrpcException;

alias Channel = GrpcClient;
class GrpcClient
{
    this(string host , ushort port)
    {
        this();
        connect(host , port);
    }


    this()
    {
        _http2Configuration = new HTTP2Configuration();
        _http2Configuration.setSecureConnectionEnabled(false);
        _http2Configuration.setFlowControlStrategy("simple");
        _http2Configuration.getTcpConfiguration().setTimeout(60 * 1000);
        _http2Configuration.setProtocol(HttpVersion.HTTP_2.asString());

        _promise = new FuturePromise!(HTTPClientConnection)();
        _client = new HTTP2Client(_http2Configuration);
    }


    void connect(string host , ushort port)
    {
        _host = host;
        _port = port;
        _client.connect(host , port , _promise,
        new class ClientHTTP2SessionListener {

            override
            Map!(int, int) onPreface(Session session) {
                Map!(int, int) settings = new HashMap!(int, int)();
                settings.put(SettingsFrame.HEADER_TABLE_SIZE, _http2Configuration.getMaxDynamicTableSize());
                settings.put(SettingsFrame.INITIAL_WINDOW_SIZE, _http2Configuration.getInitialStreamSendWindow());
                return settings;
            }

            override
            StreamListener onNewStream(Stream stream, HeadersFrame frame) {
                return null;
            }

            override
            void onSettings(Session session, SettingsFrame frame) {
            }

            override
            void onPing(Session session, PingFrame frame) {
            }

            override
            void onReset(Session session, ResetFrame frame) {
                logInfo("onReset");
            }

            override
            void onClose(Session session, GoAwayFrame frame) {
                logInfo("onClose");
            }

            override
            void onFailure(Session session, Exception failure) {
                logInfo("onFailure");
            }

            override
            bool onIdleTimeout(Session session) {
                return false;
            }
        });
    }

    ubyte[] send(string path , ubyte[] data)
    {
        ubyte[] result;
        sendAsync(path , data , (Result!(ubyte[]) data){
            if(data.failed())
                 throw data.cause();
            result = data.result;
        });

        uint tick = 0;
        import core.thread;
        while(result.length == 0)
        {
            tick++;
            if(tick > 1000)
                break;
            Thread.sleep(dur!"msecs"(10));
        }
        if(tick > 1000)
            throw new GrpcTimeoutException("timeout");
        
        return result;
    }



    void sendAsync(string path , ubyte[] data , void delegate(Result!(ubyte[]) ) dele)
    {

        HttpFields fields = new HttpFields();
        fields.put("te", "trailers");
        fields.put("content-type" ,"application/grpc+proto");
        fields.put("grpc-accept-encoding" , "identity");
        fields.put("accept-encoding" , "identity");

        // new stream
        //header
        MetaData.Request metaData = new MetaData.Request("POST", HttpScheme.HTTP,
        new HostPortHttpField(format("%s:%d", _host, _port)), 
        path,
            HttpVersion.HTTP_2, 
            fields);

        auto conn = _promise.get();
        auto client = cast(HTTP2ClientConnection)conn;
        auto streampromise = new FuturePromise!(Stream)();
        auto http2session = client.getHttp2Session();

        http2session.newStream(new HeadersFrame(metaData , null , false) ,
        streampromise , new class StreamListener {

            /// unused
            	override
			StreamListener onPush(Stream stream,
					PushPromiseFrame frame) {
				logInfo("onPush");
                return null;
			}
            /// unused
            override
            void onReset(Stream stream, ResetFrame frame, Callback callback) {
                logInfo("onReset");
                try {
					onReset(stream, frame);
					callback.succeeded();
				} catch (Exception x) {
					callback.failed(x);
				}
			}
             /// unused
			override
			void onReset(Stream stream, ResetFrame frame) {
				logInfo("onReset2");
			}
            /// unused
			override
			bool onIdleTimeout(Stream stream, Exception x) {
                logInfo("timeout");
				return true;
			}
            /// unused
			override string toString()
			{
				return super.toString();
			}



            			override
			void onHeaders(Stream stream, HeadersFrame frame) {
				logInfo("client received headers ", frame.toString());
			}

			override
			void onData(Stream stream, DataFrame frame, Callback callback) {
              
                auto bytes = cast(ubyte[])BufferUtils.toString(frame.getData());
                if(bytes.length < 5)
                {
                    dele(new Result!(ubyte[])(new GrpcDataErrorException("data len error")));
                    return;
                }               
                auto result = new Result!(ubyte[])(bytes[5 .. $].dup);
                dele(result);
                logInfo("client received data " , result.result);
			}
        });

        auto stream =  streampromise.get();
        
        ubyte compress = 0;
        
        import std.bitmanip;
        ubyte[4] len = nativeToBigEndian(cast(int)data.length);
        ubyte[] grpc_data;
        grpc_data ~= compress;
        grpc_data ~= len;
        grpc_data ~= data;
        DataFrame smallDataFrame = new DataFrame(stream.getId(),
			ByteBuffer.wrap(cast(byte[])grpc_data), true);

        stream.data(smallDataFrame , new class NoopCallback {

		override
		void succeeded() {
		}

		override
		void failed(Exception x) {
            logInfo("sending failed");
		}
	});
    }



protected:
    string _host;
    ushort _port;
    HTTP2Client _client;
    FuturePromise!(HTTPClientConnection) _promise;
    HTTP2Configuration  _http2Configuration;

}