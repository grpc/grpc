#include <memory>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <grpc++/grpc++.h>
#include "/home/radhikaj/grpc/src/cpp/client/secure_credentials.h"
#include "/usr/include/openssl/x509.h"
#include "/usr/include/openssl/pem.h"
#include "helloworld.grpc.pb.h"
#include <openenclave/host.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;
using namespace grpc;

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

class MyCustomAuthenticator : public grpc::MetadataCredentialsPlugin {
 public:
  MyCustomAuthenticator(const grpc::string& ticket) : ticket_(ticket) {}

  grpc::Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, grpc::string>* metadata) override {
    metadata->insert(std::make_pair("x-custom-auth-ticket", ticket_));
    printf("Get metadata called\n");
    return grpc::Status::OK;
  }

 private:
  grpc::string ticket_;
};

using namespace grpc;
class TestMetadataCredentialsPlugin : public MetadataCredentialsPlugin {
 public:
  static const char kGoodMetadataKey[];
  static const char kBadMetadataKey[];
  static const char kTestCredsPluginErrorMsg[];
  static const char kGoodGuy[];

  TestMetadataCredentialsPlugin(const grpc::string_ref& metadata_key,
                                const grpc::string_ref& metadata_value,
                                bool is_blocking, bool is_successful)
      : metadata_key_(metadata_key.data(), metadata_key.length()),
        metadata_value_(metadata_value.data(), metadata_value.length()),
        is_blocking_(is_blocking),
        is_successful_(is_successful) {}

  bool IsBlocking() const override { return is_blocking_; }

  Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, grpc::string>* metadata) override {
    assert(service_url.length() > 0UL);
    assert(method_name.length() > 0UL);
    assert(channel_auth_context.IsPeerAuthenticated());
    assert(metadata != nullptr);
    if (is_successful_) {
      metadata->insert(std::make_pair(metadata_key_, metadata_value_));
      return Status::OK;
    } else {
      return Status(StatusCode::NOT_FOUND, kTestCredsPluginErrorMsg);
    }
  }

 private:
  grpc::string metadata_key_;
  grpc::string metadata_value_;
  bool is_blocking_;
  bool is_successful_;
  

};

void
writecert ( const std::string& filename, std::string& data )
{
        std::ofstream file ( filename.c_str (), std::ios::out );

	if ( file.is_open () )
	{		
		file << data; 

		file.close ();
	}

	return;
}

void
read(const std::string& filename, std::string& data)
{
    std::ifstream file(filename.c_str(), std::ios::in);

    if (file.is_open())
    {
        std::stringstream ss;
        ss << file.rdbuf();

        file.close();

        data = ss.str();
    }

    return;
}

const char TestMetadataCredentialsPlugin::kGoodGuy[] = "Dr Jekyll";
const char TestMetadataCredentialsPlugin::kGoodMetadataKey[] =
    "test-plugin-metadata";
const char TestMetadataCredentialsPlugin::kBadMetadataKey[] =
    "TestPluginMetadata";
const char TestMetadataCredentialsPlugin::kTestCredsPluginErrorMsg[] = "Could not find plugin metadata.";

#include <stdio.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/pem.h>


void convert(char* cert_filestr, char* certificateFile)
{
    X509* x509 = NULL;
    FILE* fd = NULL, *fl = NULL;

    fl = fopen(cert_filestr, "rb");
    if (fl)
    {
        fd = fopen(certificateFile, "w+");
        if (fd)
        {
            x509 = PEM_read_X509(fl, &x509, NULL, NULL);
            if (x509)
            {
                i2d_X509_fp(fd, x509);
            }
            else
            {
                printf("failed to parse to X509 from fl");
            }
            fclose(fd);
        }
        else
        {
            printf("can't open fd");
        }
        fclose(fl);
    }
    else
    {
        printf("can't open f");
    }
}

oe_result_t enclave_identity_verifier_callback(
    oe_identity_t* identity,
    void* arg)
{
    //(void)arg;
    printf(
        "enclave_identity_verifier_callback is called with parsed report:\n");
    return OE_OK;
}


static int verify_callback(const char* target_host, const char* target_pem,
    void* userdata) {
    std::string s(target_pem);
    
    printf("Callback received, cert length =%d\n\n", s.length());
    printf("Callback received\n");

    writecert("server2.crt", s);
    
    //OpenSSL_add_all_algorithms();
    //ERR_load_crypto_strings();
    //seed_prng()

    X509 * x509 = nullptr;
    BIO* bio_cert = BIO_new_file("server2.crt", "rb");
    x509 = PEM_read_bio_X509(bio_cert, &x509, NULL, NULL);
    //PEM_read_X509(fp, &x509, NULL, NULL);
    if (!x509)
    {
        printf("Error reading x509\n");
    }
    FILE *fd = fopen("mycert.der", "w");
    i2d_X509_fp(fd, x509);
    fclose(fd);


    /*
    unsigned char *target_der;
    int len = i2d_X509(x509, NULL);

    target_der = (unsigned char*)OPENSSL_malloc(len);
    len = i2d_X509(x509, &target_der);
    if (len < 0)
    {
        printf("Error during conversion from pem to der\n");
    }
    printf("len =%d\n", len);
    std::string sder((char*)target_der);
    writecert("server2.der", sder);*/
    std::string target_der_from_file;
    read("mycert.der", target_der_from_file);
    oe_result_t t =  oe_verify_tls_cert(
        (unsigned char*)(target_der_from_file.c_str()),
        target_der_from_file.length(),
        enclave_identity_verifier_callback,
        nullptr);
    printf("oe_result_t = %d\n", t);
    
    return 0;
}

static void verify_destruct(void* userdata) { }



class GreeterClient
{
public:
  	GreeterClient ( const std::string& cert,
	                const std::string& key,
                        const std::string& root,
                        const std::string& server )
	{
		/*grpc::SslCredentialsOptions opts =
		{
			root,
			key,
			cert
		};
    auto call_creds = grpc::MetadataCredentialsFromPlugin(
    std::unique_ptr<grpc::MetadataCredentialsPlugin>(
        new TestMetadataCredentialsPlugin(TestMetadataCredentialsPlugin::kGoodMetadataKey, TestMetadataCredentialsPlugin::kGoodGuy,true, true)));
   auto channel_creds = grpc::SslCredentials ( opts );
   auto composite_creds = grpc::CompositeChannelCredentials (channel_creds, call_creds);*/
    verify_peer_options verify_options;
    verify_options.verify_peer_callback = verify_callback;
    verify_options.verify_peer_callback_userdata = nullptr;//static_cast<void*>(&userdata);
    verify_options.verify_peer_destruct = verify_destruct;
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
    pem_key_cert_pair.private_key = key.c_str();
    pem_key_cert_pair.cert_chain = cert.c_str();
     grpc_channel_credentials* ssl_creds = grpc_ssl_credentials_create(
      root.c_str(), &pem_key_cert_pair, &verify_options, nullptr);
         
        auto securechannelcreds =  std::make_shared<SecureChannelCredentials>  (ssl_creds);

      stub_ = Greeter::NewStub ( grpc::CreateChannel (
			server, securechannelcreds ) );
	}

  	std::string
	SayHello ( const std::string& user )
	{
		HelloRequest request;
		request.set_name(user);

		HelloReply reply;

		ClientContext context;

		Status status = stub_->SayHello ( &context, request, &reply );

		if ( status.ok () )
		{
			return reply.message ();
		}
		else
		{
			std::cout << status.error_code () << ": "
			          << status.error_message () << std::endl;
			return "RPC failed";
		}
  	}

private:
  	std::unique_ptr<Greeter::Stub> stub_;
    static const char *kGoodGuy ;
};

const char kGoodGuy[]  = "Dr Jekyll";




int
main ( int argc, char** argv )
{
	std::string cert;
	std::string key;
	std::string root;
	//std::string server { "localhost:50051" };
    std::string server{ "localhost:50051" };

	read ( "selfsigned2.cert", cert );
	read ( "selfsigned2.key", key );
	read ( "selfsigned2.cert", root );

    std::string target_der_from_file;
    read("cert.der", target_der_from_file);
    oe_result_t t = oe_verify_tls_cert(
        (unsigned char*)(target_der_from_file.c_str()),
        target_der_from_file.length(),
        enclave_identity_verifier_callback,
        nullptr);


   GreeterClient greeter ( cert, key, root, server );

	  std::string user ( "world" );
  	std::string reply = greeter.SayHello ( user );
   

  	std::cout << "Greeter received: " << reply << std::endl;
    reply = greeter.SayHello ( user );
   std::cout << "Greeter received: " << reply << std::endl;

  	return 0;
}
