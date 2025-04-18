#include "../../lib/lib.c"
#include "../deps/mongoose/mongoose.c"
#include "time.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

// #define HOST "localhost"
#define HOST "3.144.17.149"
#define PORT "8000"

int SHOULD_FINISH = 1 | (1 << 1);
int current = 0;

void print_msg(UWU_String *msg, char *prefix, char *action) {
  printf("%s %s: [ ", prefix, action);
  for (int i = 0; i < msg->length; i++) {
    printf("%c (%d)", msg->data[i], msg->data[i]);
    if (i + 1 < msg->length) {
      printf(", ");
    }
  }
  printf(" ]\n");
}

int flavio_counter = 0;

// Print websocket response and signal that we're done
static void fnFlavio(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_ERROR) {
    // On error, log error message
    // fprintf(stderr, "ERROR: %p %s\n", c->fd, (char *)ev_data);
  } else if (ev == MG_EV_WS_OPEN) {
    // When websocket handshake is successful, send message
    // mg_ws_send(c, "hello", 5, WEBSOCKET_OP_TEXT);
    MG_INFO(("Info: Flavio connected! Sending message to Jose"));

    char buff[] = {4, 4, 'J', 'o', 's', 'e', 4, 'H', 'o', 'l', 'a', '\0'};
    size_t buff_len = strlen(buff); // Removes terminal char

    // char buff[] = {4, 4, 'J', 'o', 's', 'e', 0, '\0'};
    // size_t buff_len = strlen(buff); // Removes terminal char

    mg_ws_send(c, buff, buff_len, WEBSOCKET_OP_BINARY);
  } else if (ev == MG_EV_WS_MSG) {
    // When we get echo response, print it
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    UWU_String msg = {
        .data = wm->data.buf,
        .length = wm->data.len,
    };
    print_msg(&msg, "Flavio", "GOT MSG");
    flavio_counter++;

    if (flavio_counter >= 2) {
      current = current | (1 << 1);
      c->is_draining = 1;
    }
  } else if (ev == MG_EV_CLOSE) {
    MG_INFO(("Disconnecting Flavio!"));
  }
}

int jose_counter = 0;
static void fnJose(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_ERROR) {
    // On error, log error message
    // fprintf(stderr, "ERROR: %p %s\n", c->fd, (char *)ev_data);
  } else if (ev == MG_EV_WS_OPEN) {
    // When websocket handshake is successful, send message
    // mg_ws_send(c, "hello", 5, WEBSOCKET_OP_TEXT);
    MG_INFO(("Info: Jose connected!"));
  } else if (ev == MG_EV_WS_MSG) {
    // When we get echo response, print it
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    UWU_String msg = {
        .data = wm->data.buf,
        .length = wm->data.len,
    };
    print_msg(&msg, "Jose", "GOT MSG");
    jose_counter++;
    if (flavio_counter >= 2) {
      current = current | 1;
      c->is_draining = 1;
    }
  } else if (ev == MG_EV_CLOSE) {
    MG_INFO(("Disconnecting Jose!"));
  }
}

void shutdown_service(int signal) {
  fprintf(stderr, "Info: Shutting down...\n");
  current = SHOULD_FINISH;
}

int main(void) {
  struct mg_mgr mgr; // Event manager
  mg_mgr_init(&mgr); // Initialise event manager

  struct sigaction action = {};
  action.sa_handler = shutdown_service;
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);

  const char *s_url = "http://" HOST ":" PORT "/?name=Flavio";
  struct mg_connection *flavio =
      mg_ws_connect(&mgr, s_url, fnFlavio, NULL, NULL); // Create client
  s_url = "http://" HOST ":" PORT "/?name=Jose";
  struct mg_connection *jose =
      mg_ws_connect(&mgr, s_url, fnJose, NULL, NULL); // Create client
  while (flavio && jose && current != SHOULD_FINISH) {
    mg_mgr_poll(&mgr, 1000); // Wait for echo
  }

  mg_mgr_free(&mgr); // Deallocate resources
  return 0;
}
