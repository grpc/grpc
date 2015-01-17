#include <gperftools/profiler.h>

int main() {
  ProfilerStart("/dev/null");
  ProfilerStop();
  return 0;
}
