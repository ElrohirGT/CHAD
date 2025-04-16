#include "../lib/lib.c"
#include "./deps/hashmap/hashmap.h"
#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>

// ***********************************************
// CONSTANTS
// ***********************************************
static const size_t MAX_MESSAGES_PER_CHAT = 100;
static const size_t MAX_CHARACTERS_INPUT = 254;

// ***********************************************
// MODEL
// ***********************************************

typedef struct {
  static UWU_User UWU_CurrentUser;

  // Stores the users the client can messages to.
  static UWU_UserList UWU_ActiveUsers;
  // A pointer to the current selected chat, set to NULL if no client is
  // selected.
  static UWU_ChatHistory *UWU_currentChat;
  // Group chat user
  static UWU_User UWU_GroupChat;
  // Saves all the chat histories.
  // Key: The name of the user this client chat chat to.
  // Value: An UWU_History item.
  struct hashmap_s chats;
} UWU_ClientState;

static UWU_ClientState *UWU_STATE = NULL;

// Connection to the websocket server
static mg_connection *ws_conn;

static const char *s_url = "ws://ws.vi-server.org/mirror/";
static const char *s_ca_path = "ca.pem";

void init_client_state(UWU_err *err) {}

void deinit_client_state(UWU_err *err) {}

// ===================================
//      UTILS
// ===================================

// ===================================
//      UPDATE
// ===================================

// Holds all logic related to receiving messages from the server.
// Print websocket response and signal that we're done
static void fn(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_OPEN) {
    c->is_hexdumping = 1;
  } else if (ev == MG_EV_CONNECT && mg_url_is_ssl(s_url)) {
    // struct mg_str ca = mg_file_read(&mg_fs_posix, s_ca_path);
    struct mg_tls_opts opts = {.name = mg_url_host(s_url)};
    mg_tls_init(c, &opts);
  } else if (ev == MG_EV_ERROR) {
    // On error, log error message
    MG_ERROR(("%p %s", c->fd, (char *)ev_data));
  } else if (ev == MG_EV_WS_OPEN) {
    // When websocket handshake is successful, send message
    mg_ws_send(c, "hello", 5, WEBSOCKET_OP_TEXT);
  } else if (ev == MG_EV_WS_MSG) {
    // When we get echo response, print it
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    printf("GOT ECHO REPLY: [%.*s]\n", (int)wm->data.len, wm->data.buf);

    switch (wm->data.buf[0]) {
    case ERROR:
      /* code */
      break;
    case LISTED_USERS:
      /* code */
      break;
    case GOT_USER:
      /* code */
      break;
    case REGISTERED_USER:
      /* code */
      break;
    case CHANGED_STATUS:
      /* code */
      break;
    case GOT_MESSAGE:
      break;
    case GOT_MESSAGES:
      break;
    default:
      // TODO: Show toast if no message is recognized
      fprintf(stderr, "Error: Unrecognized message from server!\n");
      break;
    }
  }

  if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE || ev == MG_EV_WS_MSG) {
    *(bool *)c->fn_data = true; // Signal that we're done
  }
}

/// @brief Sends a message to
void list_users_handler() {
  char data[1] = {1};
  mg_ws_send(ws_conn, data, 1, WEBSOCKET_OP_BINARY);
  printf("LIST USERS!");
}

// NOTE: MIGHT NOT BE USEFUL FOR OUR PURPOSES
void get_user_handler() {}

void tooggle_status_handler() {
  size_t username_length = UWU_CurrentUser.username.length;
  size_t length = 3 + username_length;
  char *data = (char *)malloc(length);
  if (data == NULL) {
    UWU_PANIC("Could not allocate memory to set STATUS BUSY");
  }

  data[0] = CHANGE_STATUS;
  data[1] = username_length;

  for (size_t i = 0; i < username_length; i++) {
    data[2 + i] = UWU_String_charAt(&UWU_CurrentUser.username, i);
  }

  if (UWU_CurrentUser.status == ACTIVE || UWU_CurrentUser.status == INACTIVE) {
    data[length - 1] = BUSY;
  } else {
    data[length - 1] = ACTIVE;
  }

  mg_ws_send(ws_conn, data, length, WEBSOCKET_OP_BINARY);
  free(data);
}

void get_messages_handler() {}

void send_message_handler(UWU_String *text) {
  size_t username_length = UWU_currentChat->channel_name.length;
  size_t message_length = text->length;
  size_t length = 4 + username_length + message_length;
  printf("Total length: %d\n", length);
  printf("username lenght: %d\n", username_length);
  printf("message leng: %d\n", message_length);
  char *data = (char *)malloc(length);

  if (data == NULL) {
    UWU_PANIC("Unable to allocate memory for sending message");
  }

  data[0] = SEND_MESSAGE;
  data[1] = username_length;
  for (size_t i = 0; i < username_length; i++) {
    data[2 + i] = UWU_String_charAt(&UWU_currentChat->channel_name, i);
  }
  // a | a | a a a | a | a
  // 0 | 1 | 2 3 4 | 5 | 6
  // 1   2   3 4 5   5 | 7
  data[2 + username_length] = message_length;
  for (size_t i = 0; i < message_length; i++) {
    data[3 + username_length + i] = UWU_String_charAt(text, i);
  }

  mg_ws_send(ws_conn, data, length, WEBSOCKET_OP_BINARY);

  free(data);
}

// ===================================
//      MESSAGE
// ===================================

int main(int argc, char *argv[]) {
  struct mg_mgr mgr; // Event manager
  bool done = false; // Event handler flips it to true
  int i;

  // Parse command-line flags
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-url") == 0 && argv[i + 1] != NULL) {
      s_url = argv[++i];
    } else if (strcmp(argv[i], "-ca") == 0 && argv[i + 1] != NULL) {
      // s_ca_path = argv[++i];
    } else {
      printf("Usage: %s OPTIONS\n"
             "  -ca PATH  - Path to the CA file, default: '%s'\n"
             "  -url URL  - Destination URL, default: '%s'\n",
             argv[0], s_ca_path, s_url);
      return 1;
    }
  }

  mg_mgr_init(&mgr);       // Initialise event manager
  mg_log_set(MG_LL_DEBUG); // Set log level
  ws_conn = mg_ws_connect(&mgr, s_url, fn, &done, NULL); // Create client
  while (ws_conn && done == false)
    mg_mgr_poll(&mgr, 1000); // Wait for echo
  mg_mgr_free(&mgr);         // Deallocate resources
  return 0;
}