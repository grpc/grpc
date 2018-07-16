#include <poll.h>
#include <stdio.h>
#include <sys/time.h>

int main(void)
{
  struct timeval before, after;
  size_t us;

  gettimeofday(&before, NULL);
  poll(NULL, 0, 500);
  gettimeofday(&after, NULL);

  us = (after.tv_sec - before.tv_sec) * 1000000 +
    (after.tv_usec - before.tv_usec);

  if(us < 400000) {
    puts("poll() is broken");
    return 1;
  }
  else {
    puts("poll() works");
  }
  return 0;
}
