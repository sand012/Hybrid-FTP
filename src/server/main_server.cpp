#include "ServerManager.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

static ServerState g_state;

void handle_sigint(int signal) {
  (void)signal;
  printf("\n[main_server] Caught SIGINT, shutting down...\n");
  server_stop(&g_state);
  exit(0);
}

int main(int argc, char *argv[]) {
  int port = 2121; // default control port
  if (argc >= 2) {
    port = atoi(argv[1]);
  }

  if (server_init(&g_state, port) != 0) {
    return 1;
  }

  std::signal(SIGINT, handle_sigint);

  server_run(&g_state);
  return 0;
}