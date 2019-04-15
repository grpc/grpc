#include <memory>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <grpc++/grpc++.h>

#include "helloworld.grpc.pb.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;
/////////////////////////////////Server impl
class GreeterServiceImpl final : public Greeter::Service
{
	Status SayHello ( ServerContext* context,
                          const HelloRequest* request,
                          HelloReply* reply ) override
	{
		std::string prefix ( "Hello " );

		reply->set_message ( prefix + request->name () );

		return Status::OK;
	}
};

//////////////////////////Utlity to load a key or cert
void
read ( const std::string& filename, std::string& data )
{
        std::ifstream file ( filename.c_str (), std::ios::in );

	if ( file.is_open () )
	{
		std::stringstream ss;
		ss << file.rdbuf ();

		file.close ();

		data = ss.str ();
	}

	return;
}

using namespace grpc;


class TestAuthMetadataProcessor : public AuthMetadataProcessor {
 public:
  static const char kGoodGuy[];
  static const char kGoodMetadataKey[];
  static const char kBadMetadataKey[];
  static const char kTestCredsPluginErrorMsg[]; 
  TestAuthMetadataProcessor() : is_blocking_(true) {
   printf("constructor called\n");
  }

  // Interface implementation
  bool IsBlocking() const override 
  {
  printf("IsBlocking called\n"); 
  return is_blocking_; 
  }

  Status ProcessMetadata1(const InputMetadata& auth_metadata, AuthContext* context,
                 OutputMetadata* consumed_auth_metadata,
                 OutputMetadata* response_metadata) {
  
    printf("Processing metadata on server\n");
    std::string cert = context->FindPropertyValues("x509_pem_cert").front().data();
    printf("length of cert=%d\n", cert.length());
    assert(consumed_auth_metadata != nullptr);
    assert(context != nullptr);
    assert(response_metadata != nullptr);
    auto auth_md =
        auth_metadata.find(TestAuthMetadataProcessor::kGoodMetadataKey);
    assert(!(auth_md ==  auth_metadata.end()));
    string_ref auth_md_value = auth_md->second;
    
    if (auth_md_value == kGoodGuy) {
    printf("Got good value =%s\n", kGoodGuy);
      context->AddProperty(kIdentityPropName, kGoodGuy);
      context->SetPeerIdentityPropertyName(kIdentityPropName);
      consumed_auth_metadata->insert(std::make_pair(
          string(auth_md->first.data(), auth_md->first.length()),
          string(auth_md->second.data(), auth_md->second.length())));
      return Status::OK;
    } else {
      return Status(StatusCode::UNAUTHENTICATED,
                    string("Invalid principal: ") +
                        string(auth_md_value.data(), auth_md_value.length()));
    }
  }
  Status Process(const InputMetadata& auth_metadata, AuthContext* context,
                 OutputMetadata* consumed_auth_metadata,
                 OutputMetadata* response_metadata) override {
    printf("Processing metadata on server\n");
    return ProcessMetadata1(auth_metadata, context, consumed_auth_metadata, response_metadata);
  }

 private:
  static const char kIdentityPropName[];
  bool is_blocking_;
};

const char TestAuthMetadataProcessor:: kGoodMetadataKey[] =
    "test-plugin-metadata";
const char TestAuthMetadataProcessor::kBadMetadataKey[] =
    "TestPluginMetadata";
//const char TestAuthMetadataProcessor::TestCredsPluginErrorMsg[] = "Could not find plugin metadata.";
const char TestAuthMetadataProcessor::kGoodGuy[] = "Dr Jekyll";
const char TestAuthMetadataProcessor::kIdentityPropName[] = "novel identity";

//////////////////////////////Server credentials
class SecureServerCredentials : public ServerCredentials {
public:
    explicit SecureServerCredentials(grpc_server_credentials* creds)
        : creds_(creds) {}
    ~SecureServerCredentials() override {
        grpc_server_credentials_release(creds_);
    }

    //virtual void grpc::ServerCredentials::SetAuthMetadataProcessor(const std::shared_ptr<grpc::AuthMetadataProcessor>&)

    virtual void SetAuthMetadataProcessor(
        const std::shared_ptr<AuthMetadataProcessor>& processor) {}
    
    virtual int AddPortToServer(const grpc::string& addr,
        grpc_server* server)
    {
        return grpc_server_add_secure_http2_port(server, addr.c_str(), creds_);
    }

private:
    grpc_server_credentials* creds_;
};




                                                                                                                       
void
runServer()
{
	/**
	 * [!] Be carefull here using one cert with the CN != localhost. [!]
	 **/
	std::string server_address ( "localhost:50051" );

	std::string key;
	std::string cert;
	std::string root;

	read ( "soccerlcert.pem", cert );
	read ( "soccerlkeys.txt", key );
	read ( "soccerlcert.pem", root );

	ServerBuilder builder;

	grpc::SslServerCredentialsOptions::PemKeyCertPair keycert =
	{
		key,
		cert
	};

	//grpc::SslServerCredentialsOptions sslOps;
 grpc::SslServerCredentialsOptions 
sslOps(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    sslOps.pem_root_certs = root;
    sslOps.pem_key_cert_pairs.push_back(keycert);

   
    std::shared_ptr<ServerCredentials> p = grpc::SslServerCredentials( sslOps );
    printf("Setting auth metadata processor\n");
    //p->SetAuthMetadataProcessor( std::make_shared<TestAuthMetadataProcessor>());
    builder.AddListeningPort(server_address, p);

    GreeterServiceImpl service;
    builder.RegisterService(&service);

    std::unique_ptr < Server > server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    server->Wait();
}

int
main ( int argc, char** argv )
{
	runServer();

	return 0;
}
