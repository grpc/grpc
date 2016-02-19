#define GPR_NO_AUTODETECT_PLATFORM
#define GPR_PLATFORM_STRING "custom"
#define GPR_GCC_ATOMIC 1
#define GPR_GCC_TLS 1
#define GPR_POSIX_STRING 1
#define GPR_ARCH_32 1
#define GPR_CPU_CUSTOM 1
#define GPR_CUSTOM_SOCKET 1
#define GPR_CUSTOM_SYNC 1

typedef void * gpr_mu;
typedef void * gpr_cv;
typedef void * gpr_once;
#define GPR_ONCE_INIT NULL

typedef void * grpc_pollset;
typedef void * grpc_pollset_worker;
typedef void * grpc_pollset_set;

struct grpc_closure;
typedef struct grpc_closure grpc_closure;

struct grpc_exec_ctx;
typedef struct grpc_exec_ctx grpc_exec_ctx;
