#include <iostream>

#include <thread>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"

void baz() {
  int foo = *(int*)nullptr;
}

void bar() {
  baz();
}

int main(int argc, char ** argv) {
  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({
      true, /* symbolize_stacktrace */
      true, /* use_alternate_stack */
      -1, /* alarm_on_failure_secs */
      true, /* call_previous_handler */
      nullptr, /* writerFn */
  });

  std::thread t(bar);
  t.join();
  std::cout << "Hello world" << std::endl;
  return 0;
}
