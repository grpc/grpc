#ifdef GRPCXX_USE_DLL
  #ifdef grpc___EXPORTS
    #define GRPCXX_EXPORT __declspec(dllexport)
  #else //grpc___EXPORTS
    #define GRPCXX_EXPORT __declspec(dllimport)
  #endif //grpc___EXPORTS
#else //GRPCXX_USE_DLL
  #define GRPCXX_EXPORT
#endif //GRPCXX_USE_DLL