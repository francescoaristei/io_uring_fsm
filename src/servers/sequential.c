#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "state.h"
#include <stdint.h>

#define MAXCLIENTS 1000

state_t clients[MAXCLIENTS];
int main() {

  // initialize the server  
  int list_sock = srv_init();
  while (1) {
    int connected_sock = srv_accept_new_connection(list_sock);
    clients[0] = init(); // initialize state to 'a'
    while (1) {
      message_t m = receive(connected_sock);

      clients[0] = compute_state(0, clients[0], m);

      // send back an ack for benchmarking pursposes
      sendBack(connected_sock);
      if (isExitState(clients[0])) {
        printf("peer done");
        sendDone(connected_sock);
        break;
      }
    };
  }
}
