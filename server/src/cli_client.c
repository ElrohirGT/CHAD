#include "../../lib/lib.c"
#include "../deps/mongoose/mongoose.c"
#include <stdio.h>

bool done = false;

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

static const char *s_url = "ws://localhost:8000/websocket?name=Flavio";

// Print websocket response and signal that we're done
static void fn(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_ERROR) {
    // On error, log error message
    fprintf(stderr, "%p %s\n", c->fd, (char *)ev_data);
  } else if (ev == MG_EV_WS_OPEN) {
    // When websocket handshake is successful, send message
    mg_ws_send(c, "hello", 5, WEBSOCKET_OP_TEXT);
  } else if (ev == MG_EV_WS_MSG) {
    // When we get echo response, print it
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    UWU_String msg = {
        .data = wm->data.buf,
        .length = wm->data.len,
    };
    print_msg(&msg, "CONN 1", "GOT MSG");
  }

  if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE || ev == MG_EV_WS_MSG) {
    done = true;
  }
}

int main(void) {
  struct mg_mgr mgr;                               // Event manager
  struct mg_connection *c;                         // Client connection
  mg_mgr_init(&mgr);                               // Initialise event manager
  c = mg_ws_connect(&mgr, s_url, fn, &done, NULL); // Create client
  while (c && done == false) {
    mg_mgr_poll(&mgr, 1000); // Wait for echo
  }
  mg_mgr_free(&mgr); // Deallocate resources
  return 0;
}
