#ifndef GRPCPP_IMPL_CODEGEN_INIT_H
#define GRPCPP_IMPL_CODEGEN_INIT_H

namespace grpc {
    // Predefine interfaces
    class CoreCodegenInterface;
    class GrpcLibraryInterface;
    
    // Define getters and setters for interfaces
    CoreCodegenInterface* get_g_core_codegen_interface();
    GrpcLibraryInterface* get_g_glip();
    
    void init_g_core_codegen_interface(CoreCodegenInterface*);
    void init_g_glip(GrpcLibraryInterface*);
}

#endif // GRPCPP_IMPL_CODEGEN_INIT_H