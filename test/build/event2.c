/* This is only a compilation test, to see if we have libevent installed. */

#include <event2/event.h>

int main() {
  event_base_new();
  return 0;
}
