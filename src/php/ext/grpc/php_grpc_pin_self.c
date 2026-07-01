#include <dlfcn.h>

static int grpc_php_nodelete_anchor = 0;

void grpc_php_pin_self_with_nodelete(void) {
  Dl_info info;
  if (dladdr(&grpc_php_nodelete_anchor, &info) && info.dli_fname) {
    void *h = dlopen(info.dli_fname,
                     RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
    (void)h; /* discard; NODELETE flag is now set on the loaded lib */
  }
}
