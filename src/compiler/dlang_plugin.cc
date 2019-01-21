/*
*
* Copyright 2015 gRPC authors.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/

// Generates cpp gRPC service interface out of Protobuf IDL.
//

#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"
#include "src/compiler/protobuf_plugin.h"






static void GenerateService(const std::string &module , CodedOutputStream &cos,const ServiceDescriptor *service)
{
	/// client
	cos.WriteString("class " + service->name() + "Client\n");
	cos.WriteString("{\n");
	cos.WriteString("\tthis(Channel channel)\n");
	cos.WriteString("\t{\n");
	cos.WriteString("\t\t_channel = channel;\n");
	cos.WriteString("\t}\n");
	cos.WriteString("\n");


	/// client's methods
	for (int i = 0; i < service->method_count(); i++)
	{
		auto m = service->method(i);
		auto res = m->output_type()->name();
		auto req = m->input_type()->name();
		auto func = m->name();
		
		if (m->client_streaming() && m->server_streaming())
		{
			cos.WriteString("\tClientReaderWriter!(" + res + " ," + req + ") " + func + "(){\n");
			cos.WriteString("\t\tmixin(CM3!(" + res + " , " + req + "  , " + service->name() + "Base.SERVICE));\n");
			cos.WriteString("\t}\n");
		}
		else if (m->client_streaming())
		{
			cos.WriteString("\tClientWriter!" + req + " " + func + "( ref " + res + " response ){\n");
			cos.WriteString("\t\tmixin(CM2!(" + req + " , " + service->name() + "Base.SERVICE));\n");
			cos.WriteString("\t}\n");
		}
		else if (m->server_streaming())
		{
			cos.WriteString("\tClientReader!" +res+ " " + func + "(" + req + " request ){\n");
			cos.WriteString("\t\tmixin(CM1!(" + res + " , " + service->name() + "Base.SERVICE));\n");
			cos.WriteString("\t}\n");
		}
		else {

			cos.WriteString("\t"+ res + " " + func + "( " + req + " request)\n");
			cos.WriteString("\t{\n");
			cos.WriteString("\t\tmixin(CM!("+ res +" , " + service->name() + "Base.SERVICE));\n");
			cos.WriteString("\t}\n");

			cos.WriteString("\n");

			cos.WriteString("\tvoid " + func + "( " + req + " request , void delegate(Status status , " + res + " response) dele)\n");
			cos.WriteString("\t{\n");
			cos.WriteString("\t\tmixin(CMA!(" + res + " , " + service->name() + "Base.SERVICE));\n");
			cos.WriteString("\t}\n");

			cos.WriteString("\n");
		}



	}
	cos.WriteString("\n");
	cos.WriteString("\tprivate:\n");
	cos.WriteString("\tChannel _channel;\n");

	cos.WriteString("}\n");
	cos.WriteString("\n");


	/// service
	cos.WriteString("class " + service->name() + "Base: GrpcService\n");
	cos.WriteString("{\n");
	cos.WriteString("\tenum SERVICE  = \"" + module + "." + service->name() + "\";");
	cos.WriteString("\n");
	cos.WriteString("\tstring getModule()\n");
	cos.WriteString("\t{\n");
	cos.WriteString("\t\treturn SERVICE;\n");
	cos.WriteString("\t}\n");
	cos.WriteString("\n");

	/// service's methods
	for(int i = 0 ; i < service->method_count() ; i++)
	{
		auto m = service->method(i);
		auto res = m->output_type()->name();
		auto req = m->input_type()->name();
		auto func = m->name();
		if (m->client_streaming() && m->server_streaming())
		{
			cos.WriteString("\tStatus " + func + "(ServerReaderWriter!(" + req + " , " + res + ")){ return Status.OK; }\n");
		}
		else if (m->client_streaming())
		{
			cos.WriteString("\tStatus " + func + "(ServerReader!" + req + " , ref " + res + "){ return Status.OK; }\n");
		}
		else if (m->server_streaming())
		{
			cos.WriteString("\tStatus " + func + "(" + req + " , ServerWriter!" + res + "){ return Status.OK; }\n");
		}
		else{
			cos.WriteString("\tStatus " + func + "(" + req + " , ref " + res + "){ return Status.OK; }\n");
		}
	}

	cos.WriteString("\n");
	
	/// service's process
	cos.WriteString("\tStatus process(string method , GrpcStream stream)\n");
	cos.WriteString("\t{\n");
	cos.WriteString("\t\tswitch(method)\n");
	cos.WriteString("\t\t{\n");
	for (int i = 0; i < service->method_count(); i++)
	{
		auto m = service->method(i);
		auto res = m->output_type()->name();
		auto req = m->input_type()->name();
		auto func = m->name();
		if (m->client_streaming() && m->server_streaming())
		{
			cos.WriteString("\t\t\tmixin(SM3!(" + req + " , " + res + " , \"" + func + "\"));\n");
		}
		else if (m->client_streaming())
		{
			cos.WriteString("\t\t\tmixin(SM2!(" + req + " , " + res + " , \"" + func + "\"));\n");
		}
		else if (m->server_streaming())
		{
			cos.WriteString("\t\t\tmixin(SM1!(" + req + " , " + res + " , \"" + func + "\"));\n");
		}
		else
		{ 
			cos.WriteString("\t\t\tmixin(SM!(" + req + " , " +res+ " , \"" + func + "\"));\n");
		}
	}				
	
	cos.WriteString("\t\t\tmixin(NONE());\n");
	
	cos.WriteString("\t\t}\n");
	cos.WriteString("\t}\n");
	
	cos.WriteString("}\n");
}


class DlangGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
public:
	DlangGrpcGenerator() {}
	virtual ~DlangGrpcGenerator() {}

	virtual bool Generate(const grpc::protobuf::FileDescriptor* file,
		const grpc::string& parameter,
		grpc::protobuf::compiler::GeneratorContext* context,
		grpc::string* error) const {
		
		if (file->syntax() != grpc::protobuf::FileDescriptor::Syntax::SYNTAX_PROTO3)
		{
			*error = "dlang_plugin only support proto3";
			return false;
		}

		/// filename
		grpc::string filename = grpc_generator::StripProto(file->name());
		CodedOutputStream cos(context->Open(filename + "rpc.d"));
		/// module
		{	
			grpc::string module = "module " + file->package() + "." + filename + "rpc;\n";
			cos.WriteString(module);
		}

		cos.WriteString("\n\n");

		/// 
		{
			cos.WriteString("// Generated by the gRPC dlang plugin.\n");
		}

		cos.WriteString("\n\n");

		/// import fixed file
		{
			cos.WriteString("import " + file->package() + "." + filename + ";\n");
			cos.WriteString("import std.array;\n");
			cos.WriteString("import grpc;\n");
			cos.WriteString("import google.protobuf;\n");
			cos.WriteString("import hunt.logging;\n");
			cos.WriteString("import core.thread;\n");
		}

		cos.WriteString("\n\n");

		/// import dep next
		for(int i = 0 ; i < file->dependency_count() ; i++ )
		{
			auto dep = file->dependency(i);
			if(dep->package() != "")
				cos.WriteString("import " + dep->package() + "." + grpc_generator::StripProto(dep->name()) + ";\n");
			else
				cos.WriteString("import " + grpc_generator::StripProto(dep->name()) + ";\n");
		}

		cos.WriteString("\n\n");

		/// import public dep next
		{
			for (int i = 0; i < file->public_dependency_count(); i++)
			{
				auto dep = file->public_dependency(i);
				if(dep->package() != "")
					cos.WriteString("public import " + dep->package() + "." + grpc_generator::StripProto(dep->name()) + ";\n");
				else
					cos.WriteString("public import " + grpc_generator::StripProto(dep->name()) + ";\n");
			}

		}

		/// service
		for(int i = 0 ; i < file->service_count() ; i++)
		{
			GenerateService(file->package() , cos , file->service(i));
			cos.WriteString("\n\n");
		}


		return true;
	}

};

int main(int argc, char* argv[]) {
	DlangGrpcGenerator generator;
	return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
