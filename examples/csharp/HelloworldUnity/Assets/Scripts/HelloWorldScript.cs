using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

using Helloworld;
using System.Threading.Tasks;

using System;

using UnityEngine.SceneManagement;

using Grpc.Core;

public class HelloWorldScript : MonoBehaviour {
    const int Port = 50051;
    int counter = 1;
	// Use this for initialization
	void Start () {
        //Console.WriteLine("dfsdfadfffa dfasfa");
        
		
	}

    public void RunHelloWorld(Text text)
    {
        //Debug.Log("dfasfa");
        //var channel = new Channel("localhost:12345", ChannelCredentials.Insecure);
        //SceneManager.LoadScene("RocketMouse");


        //var unityApplicationClass = Type.GetType("UnityEngine.Application, UnityEngine");
            // Consult value of Application.platform via reflection
            // https://docs.unity3d.com/ScriptReference/Application-platform.html
          //  var platformProperty = unityApplicationClass.GetTypeInfo().GetProperty("platform");
           // var unityRuntimePlatform = platformProperty?.GetValue(null)?.ToString();
            //var isUnityIOS = (unityRuntimePlatform == "IPhonePlayer");
       
        var t = Type.GetType("UnityEngine.Application, UnityEngine");
        var propInfo = t.GetProperty("platform");
        var reflPlatform = propInfo.GetValue(null).ToString();


        Debug.Log("Appl. platform:" + Application.platform);
        Debug.Log("Appl. platform:" + reflPlatform);
        Debug.Log("Environment.OSVersion: " + Environment.OSVersion);


        Server server = new Server
        {
            Services = { Greeter.BindService(new GreeterImpl()) },
            Ports = { new ServerPort("localhost", Port, ServerCredentials.Insecure) }
        };
        server.Start();

        Channel channel = new Channel("127.0.0.1:50051", ChannelCredentials.Insecure);

        var client = new Greeter.GreeterClient(channel);
        String user = "Unity " + counter;

        var reply = client.SayHello(new HelloRequest { Name = user });


        text.text = "Greeting: " + reply.Message;

        channel.ShutdownAsync().Wait();

        server.ShutdownAsync().Wait();

        counter ++;



        //Debug.Log("channel: created channel");


    }
	
	// Update is called once per frame
	void Update () {
		
	}

    class GreeterImpl : Greeter.GreeterBase
    {
        // Server side handler of the SayHello RPC
        public override Task<HelloReply> SayHello(HelloRequest request, ServerCallContext context)
        {
            return Task.FromResult(new HelloReply { Message = "Hello " + request.Name });
        }
    }
}
