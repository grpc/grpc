/*
 * This program handles SIGINT and forwards it to another process.
 * It is intended to be run as PID 1.
 *
 * Docker starts processes with "docker run" as PID 1.
 * On Linux, the default signal handler for PID 1 ignores any signals.
 * Therefore Ctrl-C aka SIGINT is ignored per default.
 */

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

int pid = 0;

void
handle_sigint (int signum)
{
  if(pid)
    kill(pid, SIGINT);
}

int main(int argc, char *argv[]){
  struct sigaction new_action;
  int status = -1;

  /* Set up the structure to specify the new action. */
  new_action.sa_handler = handle_sigint;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;

  sigaction (SIGINT, &new_action, (void*)0);

  pid = fork();
  if(pid){
    wait(&status);
    return WEXITSTATUS(status);
  }else{
    status = execvp(argv[1], &argv[1]);
    perror("exec");
    return status;
  }
}
