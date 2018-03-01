#ifdef GRPC_USE_DLLS
  #ifdef grpc___EXPORTS
    #define GRPCXX_EXPORT __declspec(dllexport)
  #else //grpc___EXPORTS
    #define GRPCXX_EXPORT __declspec(dllimport)
  #endif //grpc___EXPORTS
#else //GRPC_USE_DLLS
  #define GRPCXX_EXPORT
#endif //GRPC_USE_DLLS