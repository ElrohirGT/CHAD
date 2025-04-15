// Copyright (c) 2020 Cesanta Software Limited
// All rights reserved
//
// Example Websocket server. See https://mongoose.ws/tutorials/websocket-server/

#include "../../lib/lib.c"
#include "../deps/hashmap/hashmap.h"
#include "../deps/mongoose/mongoose.c"

/* *****************************************************************************
Constants
***************************************************************************** */
static const char *s_listen_on = "ws://localhost:8000";
static const char *s_web_root = ".";
static const char *s_ca_path = "ca.pem";
static const char *s_cert_path = "cert.pem";
static const char *s_key_path = "key.pem";

// Global group chat
static const UWU_String GROUP_CHAT_CHANNEL = {.data = "~", .length = 1};

// The max quantity of messages a chat history can hold...
// This value CAN'T be higher than 255 since that's the maximum number of
// messages that can be sent over the wire.
static const size_t MAX_MESSAGES_PER_CHAT = 100;

// The amount of seconds that need to pass in order for a user to become IDLE.
static const time_t IDLE_SECONDS_LIMIT = 15;
// The amount of seconds that we wait before checking for IDLE users again.
static const struct timespec IDLE_CHECK_FREQUENCY = {.tv_sec = 3, .tv_nsec = 0};

/* *****************************************************************************
Server State
***************************************************************************** */

// Struct to hold all the server state!
typedef struct {
  // Saves all the active usernames currently connected in this server.
  UWU_UserList active_usernames;
  // Saves all the messages from the group chat.
  UWU_ChatHistory group_chat;
  // Saves all the chat histories.
  // Key: The combination of both usernames as a UWU_String.
  // Value: An UWU_History item.
  struct hashmap_s chats;
  // Flag to alert all threads that the server is shutting off.
  // ONLY THE MAIN thread should update this value!
  UWU_Bool is_shutting_off;
  // Arena that holds the maximum amount of data a request can have.
  // This allows us to manage requests without having to allocate new memory.
  UWU_Arena req_arena;
  // Mongoose message manager.
  struct mg_mgr manager;
} UWU_ServerState;

static UWU_ServerState *UWU_STATE = NULL;

UWU_ServerState initialize_server_state(UWU_Err err) {
  UWU_ServerState state = {};

  state.active_usernames = UWU_UserList_init(err);
  if (err != NO_ERROR) {
    return state;
  }

  char *group_chat_name = malloc(sizeof(char));
  if (group_chat_name == NULL) {
    err = MALLOC_FAILED;
    return state;
  }
  *group_chat_name = '~';
  UWU_String uwu_name = {.data = group_chat_name, .length = 1};

  state.group_chat = UWU_ChatHistory_init(255, uwu_name, err);
  if (err != NO_ERROR) {
    return state;
  }

  if (0 != hashmap_create(8, &state.chats)) {
    err = HASHMAP_INITIALIZATION_ERROR;
    return state;
  }

  // The message that has the maximum size is the response to chat history!
  /* clang-format off */
  /* | type (1 byte)  | num msgs (1 byte) | length user (1 byte) | username (max 255 bytes) | length msg (1 bye) | msg (max 255 bytes) |*/
  /* clang-format on */
  size_t max_msg_size = 1 + 1 + 255 * (1 + 255 + 1 + 255);
  state.req_arena = UWU_Arena_init(max_msg_size, err);
  if (err != NO_ERROR) {
    return state;
  }

  // TODO: Initialize other server state...

  return state;
}

void deinitialize_server_state(UWU_ServerState *state) {
  state->is_shutting_off = TRUE;

  fprintf(stderr, "Cleaning User List...\n");
  UWU_UserList_deinit(&state->active_usernames);
  fprintf(stderr, "Cleaning group Chat history...\n");
  UWU_ChatHistory_deinit(&state->group_chat);
  fprintf(stderr, "Cleaning DM Chat histories...\n");
  hashmap_destroy(&state->chats);
  fprintf(stderr, "Cleaning request arena...\n");
  UWU_Arena_deinit(state->req_arena);
}

/* *****************************************************************************
Utilities functions
***************************************************************************** */
void update_last_action(UWU_User *info) {
  info->last_action = time(NULL);
  if ((time_t)-1 == info->last_action) {
    UWU_PANIC("Fatal: Failed to obtain curren time!");
    return;
  }
}

// Broadcasts an msg to all available connections!
void broadcast_msg(struct mg_mgr *mgr, UWU_String *msg) {
  for (struct mg_connection *wc = mgr->conns; wc != NULL; wc = wc->next) {
    size_t sent_count =
        mg_ws_send(wc, msg->data, msg->length, WEBSOCKET_OP_BINARY);
    if (sent_count != msg->length) {
      UWU_PANIC("Fatal: Couln't send the complete message!");
    }
  }
}

UWU_String create_changed_status_message(UWU_Arena *arena, UWU_User *info) {
  UWU_Err err = NO_ERROR;
  int data_length = 2 + info->username.length + 1;
  char *data = UWU_Arena_alloc(arena, sizeof(char) * data_length, err);

  if (err != NO_ERROR) {
    UWU_PANIC("Fatal: Failed to allocate space for message `changed_status`");
    UWU_String dummy = {};
    return dummy;
  }

  data[0] = CHANGED_STATUS;
  data[1] = info->username.length;
  for (int i = 0; i < info->username.length; i++) {
    UWU_String u = info->username;
    data[2 + i] = UWU_String_charAt(&u, i);
  }
  // memcpy(&data[2], &info->username.data, info->username.length);
  data[data_length - 1] = info->status;

  UWU_String msg = {.length = data_length, .data = data};
  return msg;
}

/* *****************************************************************************
IDLE Detector
***************************************************************************** */
static void *idle_detector(void *p) {
  UWU_Err err = NO_ERROR;
  UWU_Arena arena = UWU_Arena_init(2 + 1 + 255, err);
  if (err != NO_ERROR) {
    UWU_PANIC("Fatal: Failed to initialize idle_detector arena!");
    return NULL;
  }

  // UWU_UserList *active_usernames = (UWU_UserList *)p;
  // fprintf(stderr, "Info: active_usernames received in: %p\n",
  //         (void *)&UWU_STATE->active_usernames);
  UWU_UserList usernames = UWU_STATE->active_usernames;
  while (!UWU_STATE->is_shutting_off) {
    fprintf(stderr, "Info: Checking to IDLE %zu active users...\n",
            usernames.length);
    time_t now = time(NULL);

    if ((clock_t)-1 == now) {
      UWU_PANIC("Fatal: Failed to get current clock time!");
      UWU_Arena_deinit(arena);
      return NULL;
    }

    for (struct UWU_UserListNode *current = usernames.start; current != NULL;
         current = current->next) {
      if (current->is_sentinel) {
        continue;
      }

      time_t seconds_diff = difftime(now, current->data.last_action);
      UWU_ConnStatus status = current->data.status;
      if (seconds_diff >= IDLE_SECONDS_LIMIT && status != INACTIVE) {
        UWU_Arena_reset(&arena);
        fprintf(stderr, "Info: Updating %.*s as INACTIVE!\n",
                current->data.username.length, current->data.username.data);
        current->data.status = INACTIVE;
        UWU_String msg = create_changed_status_message(&arena, &current->data);
        broadcast_msg(&UWU_STATE->manager, &msg);
      }
    }

    nanosleep(&IDLE_CHECK_FREQUENCY, NULL);
  }

  UWU_Arena_deinit(arena);
  return NULL;
}

// This RESTful server implements the following endpoints:
//   /websocket - upgrade to Websocket, and implement websocket echo server
//   /rest - respond with JSON string {"result": 123}
//   any other URI serves static files from s_web_root
static void fn(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_OPEN) {
    // c->is_hexdumping = 1;
  } else if (ev == MG_EV_ACCEPT && mg_url_is_ssl(s_listen_on)) {
    struct mg_str ca = mg_file_read(&mg_fs_posix, s_ca_path);
    struct mg_str cert = mg_file_read(&mg_fs_posix, s_cert_path);
    struct mg_str key = mg_file_read(&mg_fs_posix, s_key_path);
    struct mg_tls_opts opts = {.ca = ca, .cert = cert, .key = key};
    mg_tls_init(c, &opts);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    if (mg_match(hm->uri, mg_str("/websocket"), NULL)) {
      // Upgrade to websocket. From now on, a connection is a full-duplex
      // Websocket connection, which will receive MG_EV_WS_MSG events.
      mg_ws_upgrade(c, hm, NULL);
    } else if (mg_match(hm->uri, mg_str("/rest"), NULL)) {
      // Serve REST response
      mg_http_reply(c, 200, "", "{\"result\": %d}\n", 123);
    } else {
      // Serve static files
      struct mg_http_serve_opts opts = {.root_dir = s_web_root};
      mg_http_serve_dir(c, ev_data, &opts);
    }
  } else if (ev == MG_EV_WS_MSG) {
    // Got websocket frame. Received data is wm->data. Echo it back!
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    mg_ws_send(c, wm->data.buf, wm->data.len, WEBSOCKET_OP_TEXT);
  }
}

int main(int argc, char *argv[]) {
  struct mg_mgr mgr; // Event manager
  int i;

  // Parse command-line flags
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-url") == 0 && argv[i + 1] != NULL) {
      s_listen_on = argv[++i];
    } else if (strcmp(argv[i], "-ca") == 0 && argv[i + 1] != NULL) {
      s_ca_path = argv[++i];
    } else if (strcmp(argv[i], "-cert") == 0 && argv[i + 1] != NULL) {
      s_cert_path = argv[++i];
    } else if (strcmp(argv[i], "-key") == 0 && argv[i + 1] != NULL) {
      s_key_path = argv[++i];
    } else {
      printf("Usage: %s OPTIONS\n"
             "  -ca PATH  - Path to the CA file, default: '%s'\n"
             "  -cert PATH  - Path to the CERT file, default: '%s'\n"
             "  -key PATH  - Path to the KEY file, default: '%s'\n"
             "  -url URL  - Listen on URL, default: '%s'\n",
             argv[0], s_ca_path, s_cert_path, s_key_path, s_listen_on);
      return 1;
    }
  }

  mg_mgr_init(&mgr); // Initialise event manager
  printf("Starting WS listener on %s/websocket\n", s_listen_on);
  mg_http_listen(&mgr, s_listen_on, fn, NULL); // Create HTTP listener
  for (;;)
    mg_mgr_poll(&mgr, 1000); // Infinite event loop
  mg_mgr_free(&mgr);
  return 0;
}
