// Copyright (c) 2020 Cesanta Software Limited
// All rights reserved
//
// Example Websocket server. See https://mongoose.ws/tutorials/websocket-server/

#include "../../lib/lib.c"
#include "../deps/hashmap/hashmap.h"
#include "../deps/mongoose/mongoose.c"
#include "pthread.h"
#include "time.h"
#include <signal.h>
#include <stdio.h>

/* *****************************************************************************
Constants
***************************************************************************** */
static const char *s_listen_on = "ws://localhost:8000";
// static const char *s_web_root = ".";
static const char *s_ca_path = "ca.pem";
static const char *s_cert_path = "cert.pem";
static const char *s_key_path = "key.pem";

// Global group chat
static const UWU_String GROUP_CHAT_CHANNEL = {.data = "~", .length = 1};

// Separator used for chat connections
static const UWU_String SEPARATOR = {.data = "&/)", .length = strlen("&/)")};

// The max quantity of messages a chat history can hold...
// This value CAN'T be higher than 255 since that's the maximum number of
// messages that can be sent over the wire.
static const size_t MAX_MESSAGES_PER_CHAT = 100;

// The amount of seconds that need to pass in order for a user to become IDLE.
// static const time_t IDLE_SECONDS_LIMIT = 15;
// The amount of seconds that we wait before checking for IDLE users again.
// static const struct timespec IDLE_CHECK_FREQUENCY = {.tv_sec = 3, .tv_nsec =
// 0};

/* *****************************************************************************
Server State
***************************************************************************** */

// Struct to hold all the server state!
typedef struct {
  // Saves all the active usernames currently connected in this server.
  UWU_UserList active_users;
  // Saves all the messages from the group chat.
  UWU_ChatHistory group_chat;
  // Saves all the chat histories.
  // Key: The combination of both usernames as a UWU_String.
  // Value: An UWU_History item.
  struct hashmap_s chats;
  // Flag to alert all threads that the server is shutting off.
  // ONLY THE MAIN thread should update this value!
  UWU_Bool is_shutting_off;
  // The maximum amount of data a request can have.
  // This allows us to manage requests without having to allocate new memory.
  size_t req_arena_max_size;
  // Mongoose message manager.
  struct mg_mgr manager;
  // The mutation mutex, every time you need to modify the global state you'll
  // need to lock this mutex!
  pthread_mutex_t mutex;
} UWU_ServerState;

static UWU_ServerState *UWU_STATE = NULL;

UWU_ServerState initialize_server_state(UWU_Err err) {
  UWU_ServerState state = {};

  mg_mgr_init(&state.manager);

  pthread_mutex_init(&state.mutex, NULL);

  state.active_users = UWU_UserList_init(err);
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
  state.req_arena_max_size = 1 + 1 + 255 * (1 + 255 + 1 + 255);

  // TODO: Initialize other server state...

  return state;
}

void deinitialize_server_state(UWU_ServerState *state) {
  state->is_shutting_off = TRUE;

  MG_INFO(("Deinitializing mongoose manager..."));
  mg_mgr_free(&state->manager);

  MG_INFO(("Cleaning User List..."));
  UWU_UserList_deinit(&state->active_users);

  MG_INFO(("Cleaning group Chat history..."));
  UWU_ChatHistory_deinit(&state->group_chat);

  MG_INFO(("Cleaning DM Chat histories..."));
  hashmap_destroy(&state->chats);
}

// Information associated with a specific connection.
typedef struct {
  UWU_String username;
  UWU_Arena resp_arena;
} UWU_WSConnInfo;

// Creates a WSConnInfo copying the username.
void UWU_WSConnInfo_init(UWU_WSConnInfo *info, UWU_String *username,
                         UWU_Arena req_arena, UWU_Err err) {
  UWU_String copy = UWU_String_copy(username, err);
  if (err != NO_ERROR) {
    return;
  }
  info->username = copy;
  info->resp_arena = req_arena;
}

// Frees the username and deinits the arena.
void UWU_WSConnInfo_deinit(UWU_WSConnInfo *info) {
  UWU_String_freeWithMalloc(&info->username);
  UWU_Arena_deinit(info->resp_arena);
}

/* *****************************************************************************
Utilities functions
***************************************************************************** */
int remove_if_matches(void *context, struct hashmap_element_s *const e) {
  UWU_String *user_name = context;
  UWU_String hash_key = {
      .data = (char *)e->key,
      .length = e->key_len,
  };

  UWU_String tmp_after = UWU_String_combineWithOther(user_name, &SEPARATOR);
  UWU_String tmp_before = UWU_String_combineWithOther(&SEPARATOR, user_name);

  UWU_Bool starts_with_username = UWU_String_startsWith(&hash_key, &tmp_after);
  UWU_Bool ends_with_username = UWU_String_endsWith(&hash_key, &tmp_before);

  UWU_String_freeWithMalloc(&tmp_after);
  UWU_String_freeWithMalloc(&tmp_before);

  if (starts_with_username || ends_with_username) {
    UWU_ChatHistory *data = e->data;

    UWU_ChatHistory_deinit(data);
    free(data);
    return -1;
  }

  return 0;
}

void update_last_action(UWU_User *info) {
  info->last_action = time(NULL);
  if ((time_t)-1 == info->last_action) {
    UWU_PANIC("Fatal: Failed to obtain current time!\n");
    return;
  }
}

// Send a message to a specific connection.
void send_msg(struct mg_connection *conn, const UWU_String *const msg) {
  UWU_print_msg(msg, "Debug: Server", "Sends");
  size_t sent_count =
      mg_ws_send(conn, msg->data, msg->length, WEBSOCKET_OP_BINARY);
  if (sent_count < msg->length) {
    UWU_PANIC("Fatal: Couln't send the complete message! %d != %d.\n",
              sent_count, msg->length);
  }
}

// Broadcasts an msg to all available connections!
void broadcast_msg(UWU_String *msg) {
  for (struct UWU_UserListNode *current = UWU_STATE->active_users.start;
       current != NULL; current = current->next) {
    if (current->is_sentinel) {
      continue;
    }

    send_msg(current->data.conn, msg);
  }
}

UWU_String changed_status_builder(char *buff, UWU_User *info) {
  UWU_String def = {};
  size_t msg_length = 0;

  buff[msg_length] = CHANGED_STATUS;
  msg_length++;

  buff[msg_length] = info->username.length;
  msg_length++;

  for (int i = 0; i < info->username.length; i++) {
    buff[msg_length] = UWU_String_getChar(&info->username, i);
    msg_length++;
  }

  buff[msg_length] = info->status;
  msg_length++;

  def.data = buff;
  def.length = msg_length;
  return def;
}

UWU_String create_changed_status_message(UWU_Arena *arena, UWU_User *info) {
  UWU_Err err = NO_ERROR;
  int data_length = 2 + info->username.length + 1;
  char *data = UWU_Arena_alloc(arena, sizeof(char) * data_length, err);

  if (err != NO_ERROR) {
    UWU_PANIC("Fatal: Failed to allocate space for message `changed_status`\n");
    UWU_String dummy = {};
    return dummy;
  }

  return changed_status_builder(data, info);

  // data[0] = CHANGED_STATUS;
  // data[1] = info->username.length;
  // for (int i = 0; i < info->username.length; i++) {
  //   UWU_String u = info->username;
  //   data[2 + i] = UWU_String_charAt(&u, i);
  // }
  // // memcpy(&data[2], &info->username.data, info->username.length);
  // data[data_length - 1] = info->status;
  //
  // UWU_String msg = {.length = data_length, .data = data};
  // return msg;
}

/* *****************************************************************************
IDLE Detector
***************************************************************************** */
// static void *idle_detector(void *p) {
//   UWU_Err err = NO_ERROR;
//   UWU_Arena arena = UWU_Arena_init(2 + 1 + 255, err);
//   if (err != NO_ERROR) {
//     UWU_PANIC("Fatal: Failed to initialize idle_detector arena!");
//     return NULL;
//   }
//
//   // UWU_UserList *active_usernames = (UWU_UserList *)p;
//   // fprintf(stderr, "Info: active_usernames received in: %p",
//   //         (void *)&UWU_STATE->active_usernames);
//   UWU_UserList usernames = UWU_STATE->active_usernames;
//   while (!UWU_STATE->is_shutting_off) {
//     fprintf(stderr, "Info: Checking to IDLE %zu active users...",
//             usernames.length);
//     time_t now = time(NULL);
//
//     if ((clock_t)-1 == now) {
//       UWU_PANIC("Fatal: Failed to get current clock time!");
//       UWU_Arena_deinit(arena);
//       return NULL;
//     }
//
//     for (struct UWU_UserListNode *current = usernames.start; current != NULL;
//          current = current->next) {
//       if (current->is_sentinel) {
//         continue;
//       }
//
//       time_t seconds_diff = difftime(now, current->data.last_action);
//       UWU_ConnStatus status = current->data.status;
//       if (seconds_diff >= IDLE_SECONDS_LIMIT && status != INACTIVE) {
//         UWU_Arena_reset(&arena);
//         fprintf(stderr, "Info: Updating %.*s as INACTIVE!",
//                 current->data.username.length, current->data.username.data);
//         current->data.status = INACTIVE;
//         UWU_String msg = create_changed_status_message(&arena,
//         &current->data); broadcast_msg(&UWU_STATE->manager, &msg);
//       }
//     }
//
//     nanosleep(&IDLE_CHECK_FREQUENCY, NULL);
//   }
//
//   UWU_Arena_deinit(arena);
//   return NULL;
// }

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
    // We treat all requests as attempting to connect to the server...
    if (hm->query.len < 6) {
      MG_ERROR(("Query must contain at least a `name` parameter!"));
      mg_http_reply(c, 400, "", "INVALID USERNAME QUERY FORMAT");
      return;
    }

    int variable_name_length = 5;
    UWU_String EXPECTED_QUERY_START = {
        .data = "name=",
        .length = variable_name_length,
    };
    UWU_String query_start = {
        .data = hm->query.buf,
        .length = variable_name_length,
    };

    if (!UWU_String_equal(&EXPECTED_QUERY_START, &query_start)) {
      MG_ERROR(("Invalid query format!"));
      mg_http_reply(c, 400, "", "INVALID USERNAME QUERY FORMAT");
      return;
    }
    if (hm->query.len - variable_name_length <= 0) {
      MG_ERROR(("Username is too short!"));
      mg_http_reply(c, 400, "", "USERNAME CANT BE EMPTY");
      return;
    }
    if (hm->query.len - variable_name_length > 255) {
      MG_ERROR(("Username is too large!"));
      mg_http_reply(c, 400, "", "USERNAME TOO LARGE");
      return;
    }

    UWU_String source_username = {
        .data = &hm->query.buf[variable_name_length],
        .length = hm->query.len - variable_name_length,
    };

    if (UWU_String_equal(&GROUP_CHAT_CHANNEL, &source_username)) {
      MG_ERROR(("Can't connect with the same name as the group chat!"));
      mg_http_reply(c, 400, "", "INVALID USERNAME");
      return;
    }

    {
      UWU_User *user =
          UWU_UserList_findByName(&UWU_STATE->active_users, &source_username);
      if (user != NULL) {
        MG_ERROR(("Can't connect to an already used username!"));
        mg_http_reply(c, 400, "", "INVALID USERNAME");
        return;
      }
    }

    UWU_User user = {.username = source_username, .status = ACTIVE, .conn = c};
    update_last_action(&user);

    UWU_Err err = NO_ERROR;
    struct UWU_UserListNode node = UWU_UserListNode_newWithValue(user);
    UWU_UserList_insertEnd(&UWU_STATE->active_users, &node, err);
    if (err != NO_ERROR) {
      UWU_PANIC("Fatal: Failed to add username `%.*s` to the UserCollection!\n",
                source_username.length, source_username.data);
      return;
    }
    MG_INFO(("Currently %zu active users!", UWU_STATE->active_users.length));

    for (struct UWU_UserListNode *current = UWU_STATE->active_users.start;
         current != NULL; current = current->next) {

      if (current->is_sentinel) {
        continue;
      }

      UWU_String current_username = current->data.username;
      UWU_String *first = &current_username;
      UWU_String *other = &source_username;

      if (!UWU_String_firstGoesFirst(first, other)) {
        first = &source_username;
        other = &current_username;
      }

      UWU_String tmp = UWU_String_combineWithOther(first, &SEPARATOR);
      UWU_String combined = UWU_String_combineWithOther(&tmp, other);
      UWU_String_freeWithMalloc(&tmp);

      UWU_ChatHistory *ht = malloc(sizeof(UWU_ChatHistory));
      *ht = UWU_ChatHistory_init(MAX_MESSAGES_PER_CHAT, combined, err);
      if (0 !=
          hashmap_put(&UWU_STATE->chats, combined.data, combined.length, ht)) {
        UWU_PANIC("Fatal: Error creating shared chat for `%.*s`!\n",
                  combined.length, combined.data);
        return;
      }
    }

    // Tell all other users that a new connection has arrived...
    {
      size_t max_length = 3 + 255;
      char buff[max_length];
      UWU_String msg = changed_status_builder(buff, &user);

      for (struct UWU_UserListNode *current = UWU_STATE->active_users.start;
           current != NULL; current = current->next) {

        if (current->is_sentinel ||
            UWU_String_equal(&current->data.username, &user.username)) {
          continue;
        }

        MG_INFO(("Sending welcome of `%.*s` to `%.*s`",
                 (int)source_username.length, source_username.data,
                 (int)current->data.username.length,
                 current->data.username.data));

        send_msg(current->data.conn, &msg);
      }
    }

    c->fn_data = malloc(sizeof(UWU_WSConnInfo));
    {
      if (c->fn_data == NULL) {
        MG_ERROR(("Error: Can't allocate enough memory to save username!"));
        mg_http_reply(c, 500, "", "RAN OUT OF MEMORY");
        return;
      }

      UWU_Arena arena = UWU_Arena_init(UWU_STATE->req_arena_max_size, err);
      if (err != NO_ERROR) {
        MG_ERROR(("Error: Can't allocate enough memory to copy username!"));
        mg_http_reply(c, 500, "", "RAN OUT OF MEMORY");
        return;
      }

      UWU_WSConnInfo_init(c->fn_data, &source_username, arena, err);
      if (err != NO_ERROR) {
        MG_ERROR(("Error: Can't allocate enough memory to copy username!"));
        mg_http_reply(c, 500, "", "RAN OUT OF MEMORY");
        return;
      }
    }
    // c->data[0] = 10;

    mg_ws_upgrade(c, hm, NULL);
    // Serve REST response
    // mg_http_reply(c, 200, "", "{\"result\": %d}", 123);
  } else if (ev == MG_EV_WS_MSG) {
    // Got websocket frame. Received data is wm->data. Echo it back!
    // mg_ws_send(c, wm->data.buf, wm->data.len, WEBSOCKET_OP_TEXT);

    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    char *msg_data = wm->data.buf;
    size_t msg_len = wm->data.len;

    UWU_WSConnInfo *conn_info = c->fn_data;
    UWU_Arena_reset(&conn_info->resp_arena);
    UWU_Err err = NO_ERROR;

    if (wm->data.len <= 0) {
      MG_ERROR(("Error: Message is too short!"));
      return;
    }

    switch (msg_data[0]) {
    case GET_USER: {
      if (msg_len < 3) {
        MG_ERROR(("Error: Message is too short!"));
        return;
      }

      char username_length = msg_data[1];

      UWU_String user_to_get = {.data = &msg_data[2],
                                .length = username_length};

      UWU_User *user =
          UWU_UserList_findByName(&UWU_STATE->active_users, &user_to_get);

      if (user == NULL) {
        MG_ERROR(("Error: User not found!"));
        return;
      }

      printf("Username: %.*s\n", (int)user->username.length,
             user->username.data);
      printf("Status: %d\n", user->status);

      size_t response_size = user->username.length + 2;

      char *data = UWU_Arena_alloc(&conn_info->resp_arena, response_size, err);
      if (err != NO_ERROR) {
        MG_ERROR(("Error: Memory allocation failed!"));
        return;
      }

      data[0] = GOT_USER;
      memcpy(data + 1, user->username.data, user->username.length);
      data[user->username.length + 1] = (char)user->status;

      UWU_String response = {.data = data, .length = response_size};
      send_msg(c, &response);
    } break;
    case LIST_USERS: {
      char *data =
          UWU_Arena_alloc(&conn_info->resp_arena,
                          2 + (255 + 1) * UWU_STATE->active_users.length, err);
      if (err != NO_ERROR) {
        UWU_PANIC("Fatal: Allocation of memory for response failed!");
        return;
      }

      data[0] = LISTED_USERS;
      data[1] = UWU_STATE->active_users.length;

      size_t data_length = 2;
      for (struct UWU_UserListNode *current = UWU_STATE->active_users.start;
           current != NULL; current = current->next) {

        if (current->is_sentinel) {
          continue;
        }

        if (UWU_String_equal(&conn_info->username, &current->data.username)) {
          update_last_action(&current->data);
        }

        size_t username_length = current->data.username.length;
        data[data_length] = username_length;
        data_length++;

        memcpy(&data[data_length], current->data.username.data,
               username_length);
        data_length += username_length;

        data[data_length] = current->data.status;
        data_length++;
      }

      UWU_String response = {.data = data, .length = data_length};
      send_msg(c, &response);
    } break;
    case CHANGE_STATUS: {
      // Message should contain at least a username length
      if (msg_len < 2) {
        fprintf(stderr, "Error: Message is too short!\n");
        return;
      }
      char username_length = msg_data[1];
      if (username_length <= 0) {
        fprintf(stderr, "Error: The username is too short!\n");
        return;
      }

      UWU_String req_username = {
          .data = &msg_data[2],
          .length = username_length,
      };

      if (!UWU_String_equal(&req_username, &conn_info->username)) {
        fprintf(stderr,
                "Error: Another username can't change the status of the "
                "current username!\n");
        return;
      }

      UWU_User *old_user =
          UWU_UserList_findByName(&UWU_STATE->active_users, &req_username);
      if (NULL == old_user) {
        UWU_PANIC("Fatal: No active user with the given username found!");
        return;
      }

      UWU_User new_user = {
          .username = req_username,
          .status = msg_data[2 + username_length],
      };

      if (old_user->status == new_user.status) {
        fprintf(stderr, "Warning: Can't change status to the same status!\n");
        return;
      }

      UWU_Bool transition_matrix[4][4] = {};
      transition_matrix[DISCONNETED][DISCONNETED] = TRUE;

      transition_matrix[ACTIVE][BUSY] = TRUE;
      transition_matrix[BUSY][ACTIVE] = TRUE;

      transition_matrix[INACTIVE][ACTIVE] = TRUE;
      transition_matrix[INACTIVE][BUSY] = TRUE;

      UWU_Bool valid_transition =
          transition_matrix[old_user->status][new_user.status];
      if (!valid_transition) {
        fprintf(stderr, "Error: Invalid transition of user state!\n");
        char err_data[] = {(char)ERROR, (char)INVALID_STATUS};

        UWU_String err_response = {.data = err_data, .length = 2};
        send_msg(c, &err_response);
        return;
      }
      fprintf(stderr, "Info: Changing status %.*s to %d",
              (int)new_user.username.length, new_user.username.data,
              new_user.status);

      old_user->status = new_user.status;
      old_user->username = req_username;
      update_last_action(old_user);

      UWU_String response =
          create_changed_status_message(&conn_info->resp_arena, &new_user);
      broadcast_msg(&response);
    } break;
    case SEND_MESSAGE: {
      if (msg_len < 2) {
        fprintf(stderr, "Error: Message is too short!\n");
        return;
      }

      char username_length = msg_data[1];
      char message_length = msg_data[2 + username_length];

      // Message is empty
      if (message_length <= 0) {
        char error[2];
        error[0] = ERROR;
        error[1] = EMPTY_MESSAGE;

        UWU_String response = {.data = error, .length = 2};
        send_msg(c, &response);
        return;
      }

      UWU_String msg_username = {.data = &msg_data[2],
                                 .length = username_length};

      UWU_String content = {.data = &msg_data[3 + username_length],
                            .length = message_length};

      if (UWU_String_equal(&msg_username, &GROUP_CHAT_CHANNEL)) {
        fprintf(stderr, "Info: Sending message to general chat...\n");
        UWU_ChatEntry entry = {.content = content,
                               .origin_username = GROUP_CHAT_CHANNEL};
        UWU_ChatHistory_addMessage(&UWU_STATE->group_chat, &entry);

        size_t data_length = 3 + 1 + message_length;
        char *data = UWU_Arena_alloc(&conn_info->resp_arena, data_length, err);
        if (err != NO_ERROR) {
          UWU_PANIC(
              "Fatal: Failed to allocate memory for GOT_MESSAGE response!");
          return;
        }

        data[0] = GOT_MESSAGE;
        data[1] = 1;
        data[2] = '~';
        data[3] = message_length;
        for (size_t i = 0; i < message_length; i++) {
          data[4 + i] = UWU_String_charAt(&content, i);
        }

        UWU_String response = {.data = data, .length = data_length};
        broadcast_msg(&response);

        for (struct UWU_UserListNode *current = UWU_STATE->active_users.start;
             current != NULL; current = current->next) {

          if (current->is_sentinel) {
            continue;
          }
          UWU_String current_username = current->data.username;

          if (UWU_String_equal(&current_username, &conn_info->username)) {
            update_last_action(&current->data);

            if (current->data.status == INACTIVE) {
              current->data.status = ACTIVE;
              UWU_String response = create_changed_status_message(
                  &conn_info->resp_arena, &current->data);
              broadcast_msg(&response);
            }
          }
        }

      } else {

        UWU_String *first = &conn_info->username;
        UWU_String *other = &msg_username;

        if (!UWU_String_firstGoesFirst(first, other)) {
          first = &msg_username;
          other = &conn_info->username;
        }

        UWU_String tmp = UWU_String_combineWithOther(first, &SEPARATOR);
        UWU_String combined = UWU_String_combineWithOther(&tmp, other);
        UWU_String_freeWithMalloc(&tmp);

        UWU_ChatHistory *history = (UWU_ChatHistory *)hashmap_get(
            &UWU_STATE->chats, combined.data, combined.length);

        if (history == NULL) {
          UWU_PANIC("Fatal: No chat history found for key: %.*s",
                    combined.length, combined.data);
          return;
        }

        // UWU_String origin_user = {.data = conn_username->data,
        //                           .length = conn_username->length};

        UWU_ChatEntry entry = {.content = content,
                               .origin_username = conn_info->username};

        UWU_ChatHistory_addMessage(history, &entry);

        size_t data_length = 4 + conn_info->username.length + message_length;
        char *data = UWU_Arena_alloc(&conn_info->resp_arena, data_length, err);
        if (err != NO_ERROR) {
          UWU_PANIC(
              "Fatal: Failed to allocate memory for GOT_MESSAGE response!");
          return;
        }

        data[0] = GOT_MESSAGE;
        data[1] = conn_info->username.length;

        for (size_t i = 0; i < conn_info->username.length; i++) {
          data[2 + i] = UWU_String_charAt(&conn_info->username, i);
        }

        data[2 + conn_info->username.length] = message_length;
        for (size_t i = 0; i < message_length; i++) {
          data[2 + conn_info->username.length + 1 + i] =
              UWU_String_charAt(&content, i);
        }

        // channel = combinación de conn_username y el req_username
        for (struct UWU_UserListNode *current = UWU_STATE->active_users.start;
             current != NULL; current = current->next) {

          if (current->is_sentinel) {
            continue;
          }

          UWU_String current_username = current->data.username;

          if (UWU_String_equal(&current_username, &conn_info->username)) {
            update_last_action(&current->data);
          }

          if (UWU_String_equal(&current_username, &conn_info->username) ||
              UWU_String_equal(&current_username, &msg_username)) {

            if (current->data.status == INACTIVE) {
              current->data.status = ACTIVE;
              UWU_String response = create_changed_status_message(
                  &conn_info->resp_arena, &current->data);
              broadcast_msg(&response);
            }

            UWU_String response = {.data = data, .length = data_length};
            send_msg(c, &response);
          }
        }
      }
    } break;

    case GET_MESSAGES: {
      if (msg_len < 2) {
        fprintf(stderr, "Error: Message is too short!\n");
        return;
      }

      char username_length = msg_data[1];
      if (username_length <= 0) {
        fprintf(stderr, "Error: The username is too short!\n");
        return;
      }

      UWU_String req_username = {
          .data = &msg_data[2],
          .length = username_length,
      };

      if (UWU_String_equal(&req_username, &GROUP_CHAT_CHANNEL)) {
        size_t max_msg_size = 1 + 1 + 255 * (1 + 255 + 1 + 255);
        char *data = UWU_Arena_alloc(&conn_info->resp_arena, max_msg_size, err);
        if (err != NO_ERROR) {
          UWU_PANIC(
              "Fatal: Arena couldn't allocate enough memory for message!");
          return;
        }

        data[0] = GOT_MESSAGES;
        data[1] = UWU_STATE->group_chat.count;
        size_t data_length = 2;

        UWU_ChatHistory_Iterator iter =
            UWU_ChatHistory_iter(&UWU_STATE->group_chat);
        for (size_t i = iter.start; i < iter.end; i++) {
          UWU_ChatEntry entry = UWU_ChatHistory_get(
              &UWU_STATE->group_chat, i % UWU_STATE->group_chat.capacity);

          data[data_length] = entry.origin_username.length;
          data_length++;

          for (size_t j = 0; j < entry.origin_username.length; j++) {
            data[data_length] = UWU_String_getChar(&entry.origin_username, j);
            data_length++;
          }

          data[data_length] = entry.content.length;
          data_length++;

          for (size_t j = 0; j < entry.content.length; j++) {
            data[data_length] = UWU_String_getChar(&entry.content, j);
            data_length++;
          }
        }

        UWU_String response = {.data = data, .length = data_length};
        send_msg(c, &response);
      } else {
        UWU_String *first = &req_username;
        UWU_String *other = &conn_info->username;

        if (!UWU_String_firstGoesFirst(first, other)) {
          first = &conn_info->username;
          other = &req_username;
        }

        UWU_String tmp = UWU_String_combineWithOther(first, &SEPARATOR);
        UWU_String combined = UWU_String_combineWithOther(&tmp, other);
        UWU_String_freeWithMalloc(&tmp);

        UWU_ChatHistory *chat =
            hashmap_get(&UWU_STATE->chats, combined.data, combined.length);
        if (NULL == chat) {
          fprintf(stderr, "Error: Can't get chat associated with: %.*s",
                  (int)combined.length, combined.data);
          return;
        }

        size_t max_msg_size = 1 + 1 + 255 * (1 + 255 + 1 + 255);
        char *data = UWU_Arena_alloc(&conn_info->resp_arena, max_msg_size, err);
        if (err != NO_ERROR) {
          UWU_PANIC(
              "Fatal: Arena couldn't allocate enough memory for message!");
          return;
        }

        data[0] = GOT_MESSAGES;
        data[1] = chat->count;
        size_t data_length = 2;

        UWU_ChatHistory_Iterator iter = UWU_ChatHistory_iter(chat);
        for (size_t i = iter.start; i < iter.end; i++) {
          UWU_ChatEntry entry = UWU_ChatHistory_get(chat, i % chat->capacity);

          data[data_length] = entry.origin_username.length;
          data_length++;

          for (size_t j = 0; j < entry.origin_username.length; j++) {
            data[data_length] = UWU_String_getChar(&entry.origin_username, j);
            data_length++;
          }

          data[data_length] = entry.content.length;
          data_length++;

          for (size_t j = 0; j < entry.content.length; j++) {
            data[data_length] = UWU_String_getChar(&entry.content, j);
            data_length++;
          }
        }

        UWU_String response = {.data = data, .length = data_length};
        send_msg(c, &response);
      }

    } break;

    default:
      fprintf(stderr, "Error: Unrecognized message!\n");
      return;
    }

    // Connection closed!
  } else if (ev == MG_EV_CLOSE) {
    UWU_WSConnInfo *conn_info = c->fn_data;
    // This may be null when we free the manager!
    if (conn_info == NULL) {
      return;
    }

    UWU_User user = {.username = conn_info->username, .status = DISCONNETED};

    MG_INFO(("Disconnecting %.*s", (int)conn_info->username.length,
             conn_info->username.data));

    UWU_UserList_removeByUsernameIfExists(&UWU_STATE->active_users,
                                          &conn_info->username);
    hashmap_iterate_pairs(&UWU_STATE->chats, remove_if_matches, conn_info);

    int max_length = 3 + 255;
    char buff[max_length];
    UWU_String msg = changed_status_builder(buff, &user);
    MG_INFO(("Broadcasting %.*s disconnection ",
             (int)conn_info->username.length, conn_info->username.data));
    broadcast_msg(&msg);

    UWU_WSConnInfo_deinit(conn_info);
    free(conn_info);
  }
}

void shutdown_server(int signal) {
  MG_INFO(("Shutting down server..."));
  UWU_STATE->is_shutting_off = TRUE;
}

void *shutdown_with_time(void *a) {
  const struct timespec wait_time = {.tv_sec = 5, .tv_nsec = 0};
  nanosleep(&wait_time, NULL);
  // shutdown_server(0);
  return NULL;
}

int main(int argc, char *argv[]) {

  struct sigaction action = {};
  action.sa_handler = shutdown_server;
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);

  pthread_t id;
  pthread_create(&id, NULL, shutdown_with_time, NULL);

  // Parse command-line flags
  for (int i = 1; i < argc; i++) {
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

  UWU_Err err = NO_ERROR;
  UWU_ServerState state = initialize_server_state(err);
  if (err != NO_ERROR) {
    fprintf(stderr, "Fatal: Failed to initialize server state! Error: %zu\n",
            *err);
    return 1;
  }
  UWU_STATE = &state;

  printf("Starting WS listener on %s/websocket\n", s_listen_on);
  mg_http_listen(&state.manager, s_listen_on, fn, NULL); // Create HTTP listener
  for (; !UWU_STATE->is_shutting_off;)
    mg_mgr_poll(&state.manager, 1000); // Infinite event loop

  pthread_join(id, NULL);

  for (struct UWU_UserListNode *current = UWU_STATE->active_users.start;
       current != NULL; current = current->next) {
    if (current->is_sentinel) {
      continue;
    }
    mg_close_conn(current->data.conn);
  }

  deinitialize_server_state(UWU_STATE);
  return 0;
}
