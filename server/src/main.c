// Copyright (c) 2020 Cesanta Software Limited
// All rights reserved
//
// Example Websocket server. See https://mongoose.ws/tutorials/websocket-server/

#include "../../lib/lib.c"
#include "../deps/hashmap/hashmap.h"
#include "../deps/mongoose/mongoose.c"
#include "pthread.h"
#include "time.h"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* *****************************************************************************
Constants
***************************************************************************** */
static const char *s_listen_on = "ws://localhost:8000";
// static const char *s_web_root = ".";
static const char *s_ca_path = "ca.pem";
static const char *s_cert_path = "cert.pem";
static const char *s_key_path = "key.pem";

static const char EXIT_PTHREAD_MSG[] = {0};
// The maximum amount of data a response can have.
// This allows us to manage requests without having to allocate new memory.
static const size_t RESP_ARENA_MAX_SIZE = 1 + 1 + 255 * (1 + 255 + 1 + 255);
// Explanation of the above constant:
/* clang-format off */
  /* | type (1 byte)  | num msgs (1 byte) | length user (1 byte) | username (max 255 bytes) | length msg (1 bye) | msg (max 255 bytes) |*/
/* clang-format on */

// The length of the maximum message the server can receive according to the
// protocol.
static const size_t REQ_ARENA_MAX_SIZE = 3 + 255;

// Global group chat
static const UWU_String GROUP_CHAT_CHANNEL = {.data = "~", .length = 1};

// Separator used for chat connections
static const UWU_String SEPARATOR = {.data = "&/)", .length = strlen("&/)")};

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
  UWU_UserList active_users;
  // Saves all the messages from the group chat.
  UWU_ChatHistory group_chat;
  // Saves all the chat histories.
  // Key: The combination of both usernames as a UWU_String.
  // Value: An UWU_History item.
  struct hashmap_s chats;
  // Lock/Unlock this mutex before/after every operation done to chats hashmap.
  pthread_mutex_t chats_mx;
  // Flag to alert all threads that the server is shutting off.
  // ONLY THE MAIN thread should update this value!
  UWU_Bool is_shutting_off;
  // Mongoose message manager.
  struct mg_mgr manager;
} UWU_ServerState;

static UWU_ServerState *UWU_STATE = NULL;

UWU_ServerState initialize_server_state(UWU_Err err) {
  UWU_ServerState state = {};

  mg_mgr_init(&state.manager);

  pthread_mutex_init(&state.chats_mx, NULL);

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

  MG_INFO(("Deinitializing mutex..."));
  pthread_mutex_destroy(&state->chats_mx);

  MG_INFO(("Cleaning DM Chat histories..."));
  hashmap_destroy(&state->chats);
}

// Information associated with a specific connection.
typedef struct {
  // The username associated with this connection.
  UWU_String username;
  // FD to a pipe writer
  int writer;
  // The ID of the pthread that handles all the messages.
  pthread_t pid;
} UWU_WSConnInfo;

// Frees the username.
void UWU_WSConnInfo_deinit(UWU_WSConnInfo *info) {
  UWU_String_freeWithMalloc(&info->username);
}

typedef struct {
  // The thread doesn't OWNS the username!
  UWU_String username;
  int reader;
  struct mg_connection *c;
} UWU_ThreadConnInfo;

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
// PLEASE lock the active_users before calling this function!
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

  while (!UWU_STATE->is_shutting_off) {
    UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->active_users.mx) != 0,
                "Fatal: Failed to lock active_users lock!");
    MG_INFO(("[IDLE detector] Checking %d connected users...",
             (int)UWU_STATE->active_users.length));
    time_t now = time(NULL);

    if ((clock_t)-1 == now) {
      UWU_PANIC("Fatal: Failed to get current clock time!");
      UWU_Arena_deinit(arena);
    } else {
      for (struct UWU_UserListNode *current = UWU_STATE->active_users.start;
           current != NULL; current = current->next) {
        if (current->is_sentinel) {
          continue;
        }

        time_t seconds_diff = difftime(now, current->data.last_action);
        UWU_ConnStatus status = current->data.status;
        if (seconds_diff >= IDLE_SECONDS_LIMIT && status == ACTIVE) {
          UWU_Arena_reset(&arena);
          MG_INFO(("Updating %.*s as INACTIVE!",
                   (int)current->data.username.length,
                   current->data.username.data));
          current->data.status = INACTIVE;
          UWU_String msg =
              create_changed_status_message(&arena, &current->data);
          broadcast_msg(&msg);
        }
      }

      UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                  "Fatal: Failed to unlock active_users lock!");
      nanosleep(&IDLE_CHECK_FREQUENCY, NULL);
    }
  }

  UWU_Arena_deinit(arena);
  return NULL;
}

void *message_handler(void *thread_info) {
  UWU_Err err = NO_ERROR;

  UWU_ThreadConnInfo *param = thread_info;
  UWU_String conn_username = param->username;
  int req_reader_fd = param->reader;
  struct mg_connection *c = param->c;

  UWU_Arena resp_arena = UWU_Arena_init(RESP_ARENA_MAX_SIZE, err);
  if (err != NO_ERROR) {
    UWU_PANIC("Fatal: Can't initialize response arena for message handler! "
              "Connection of: %.*s",
              conn_username.length, conn_username.data);
  } else {
    // Arena that will hold the request data after reading it from the pipe.
    // We add 2 bytes because we need some extra information:
    // - buff[0]: 1 if the message is from the protocol, 0 if the thread should
    // stop processing messages.
    // - buff[1]: The length of the message.
    const size_t chunk_size = 2 + REQ_ARENA_MAX_SIZE;
    UWU_Arena req_arena = UWU_Arena_init(chunk_size, err);
    if (err != NO_ERROR) {
      UWU_PANIC("Fatal: Can't initialize request arena for message handler! "
                "Connection of: %.*s",
                conn_username.length, conn_username.data);
    } else {

      struct pollfd wrapper = {
          .fd = req_reader_fd,
          .events = POLLIN,
          .revents = 0,
      };
      struct pollfd fds[] = {wrapper};
      size_t ms_wait_time = 1000;
      for (int rt = poll(fds, 1, ms_wait_time); rt >= 0;
           rt = poll(fds, 1, ms_wait_time)) {
        if (UWU_STATE->is_shutting_off) {
          break;
        }
        UWU_Arena_reset(&req_arena);
        UWU_Arena_reset(&resp_arena);

        char *buff = UWU_Arena_allocRemaining(&req_arena, err);
        if (err != NO_ERROR) {
          UWU_PANIC(
              "Fatal: Can't allocate enough memory for the request inside! "
              "Did you forget to reset the arena?");
        } else {

          int read_result = read(req_reader_fd, buff, chunk_size);
          if (read_result == -1) {
            UWU_PANIC("Fatal: An error ocurred while reading from pipe!");
          } else if (read_result == 0) {
            // EOF reached should close thread...
            break;
          } else {

            int terminate_thread = buff[0] == 0;
            if (terminate_thread) {
              break;
            }

            // size_t msg_length = &buff[1];
            char *msg_data = buff + 2;

            switch (msg_data[0]) {
            case GET_USER: {
              char username_length = msg_data[1];
              UWU_String user_to_get = {.data = &msg_data[2],
                                        .length = username_length};

              UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->active_users.mx) != 0,
                          "Fatal: Can't lock the active_users mutex!");
              UWU_User *user = UWU_UserList_findByName(&UWU_STATE->active_users,
                                                       &user_to_get);

              if (user == NULL) {
                MG_ERROR(("Error: User not found!"));
              } else {
                printf("Username: %.*s\n", (int)user->username.length,
                       user->username.data);
                printf("Status: %d\n", user->status);

                size_t response_size = user->username.length + 2;

                char *data = UWU_Arena_alloc(&resp_arena, response_size, err);
                if (err != NO_ERROR) {
                  MG_ERROR(("Error: Memory allocation failed!"));
                } else {
                  data[0] = GOT_USER;
                  memcpy(data + 1, user->username.data, user->username.length);
                  data[user->username.length + 1] = (char)user->status;

                  UWU_String response = {.data = data, .length = response_size};
                  send_msg(c, &response);
                }
              }
              UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) !=
                              0,
                          "Fatal: Can't unlock the active_users mutex!");

            } break;
            case LIST_USERS: {
              UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->active_users.mx) != 0,
                          "Fatal: Can't lock the active_users mutex!");
              char *data = UWU_Arena_alloc(
                  &resp_arena, 2 + (255 + 1) * UWU_STATE->active_users.length,
                  err);
              if (err != NO_ERROR) {
                UWU_PANIC("Fatal: Allocation of memory for response failed!");

              } else {
                data[0] = LISTED_USERS;
                data[1] = UWU_STATE->active_users.length;

                size_t data_length = 2;
                for (struct UWU_UserListNode *current =
                         UWU_STATE->active_users.start;
                     current != NULL; current = current->next) {

                  if (current->is_sentinel) {
                    continue;
                  }

                  if (UWU_String_equal(&conn_username,
                                       &current->data.username)) {
                    update_last_action(&current->data);
                  }

                  size_t username_length = current->data.username.length;
                  data[data_length] = username_length;
                  data_length++;

                  memcpy(data + data_length, current->data.username.data,
                         username_length);
                  data_length += username_length;

                  data[data_length] = current->data.status;
                  data_length++;
                }

                UWU_String response = {.data = data, .length = data_length};
                send_msg(c, &response);
              }
              UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) !=
                              0,
                          "Fatal: Can't unlock the active_users mutex!");
            } break;
            case CHANGE_STATUS: {
              // Message should contain at least a username length
              char username_length = msg_data[1];
              if (username_length <= 0) {
                MG_ERROR(("The username is too short!"));
              } else {

                UWU_String req_username = {
                    .data = &msg_data[2],
                    .length = username_length,
                };

                if (!UWU_String_equal(&req_username, &conn_username)) {
                  MG_ERROR(("Another username can't change the status of the "
                            "current username!"));
                } else {

                  UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->active_users.mx) !=
                                  0,
                              "Fatal: Can't lock the active_users mutex!");
                  UWU_User *old_user = UWU_UserList_findByName(
                      &UWU_STATE->active_users, &req_username);
                  if (NULL == old_user) {
                    UWU_PANIC(
                        "Fatal: No active user with the given username found!");
                  } else {

                    UWU_User new_user = {
                        .username = req_username,
                        .status = msg_data[2 + username_length],
                    };

                    if (old_user->status == new_user.status) {
                      MG_INFO(("Can't change status to the same status!"));
                    } else {

                      UWU_Bool transition_matrix[4][4] = {};
                      transition_matrix[DISCONNETED][DISCONNETED] = TRUE;

                      transition_matrix[ACTIVE][BUSY] = TRUE;
                      transition_matrix[BUSY][ACTIVE] = TRUE;

                      transition_matrix[INACTIVE][ACTIVE] = TRUE;
                      transition_matrix[INACTIVE][BUSY] = TRUE;

                      UWU_Bool valid_transition =
                          transition_matrix[old_user->status][new_user.status];
                      if (!valid_transition) {
                        MG_ERROR(("Invalid transition of user state!"));
                        char err_data[] = {(char)ERROR, (char)INVALID_STATUS};

                        UWU_String err_response = {.data = err_data,
                                                   .length = 2};
                        send_msg(c, &err_response);

                      } else {
                        MG_INFO(("Changing status %.*s to %d",
                                 (int)new_user.username.length,
                                 new_user.username.data, new_user.status));

                        old_user->status = new_user.status;
                        update_last_action(old_user);

                        UWU_String response = create_changed_status_message(
                            &resp_arena, &new_user);
                        broadcast_msg(&response);
                      }
                    }
                  }
                  UWU_PanicIf(
                      pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                      "Fatal: Can't lock the active_users mutex!");
                }
              }
            } break;
            case SEND_MESSAGE: {
              char username_length = msg_data[1];
              char message_length = msg_data[2 + username_length];

              // Message is empty
              if (message_length <= 0) {
                char error[2];
                error[0] = ERROR;
                error[1] = EMPTY_MESSAGE;

                UWU_String response = {.data = error, .length = 2};
                send_msg(c, &response);
              } else if (username_length <= 0) {

                char error[2];
                error[0] = ERROR;
                error[1] = USER_NOT_FOUND;

                UWU_String response = {.data = error, .length = 2};
                send_msg(c, &response);
              } else {

                UWU_String msg_username = {.data = &msg_data[2],
                                           .length = username_length};

                UWU_String content = {.data = &msg_data[3 + username_length],
                                      .length = message_length};

                if (UWU_String_equal(&msg_username, &GROUP_CHAT_CHANNEL)) {
                  MG_INFO(("Sending message to general chat..."));
                  UWU_ChatEntry entry = {.content = content,
                                         .origin_username = GROUP_CHAT_CHANNEL};
                  UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->group_chat.mx) !=
                                  0,
                              "Fatal: Can't lock the group_chat mutex!");
                  UWU_ChatHistory_addMessage(&UWU_STATE->group_chat, &entry);
                  UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->group_chat.mx) !=
                                  0,
                              "Fatal: Can't unlock the group_chat mutex!");

                  size_t data_length = 3 + 1 + message_length;
                  char *data = UWU_Arena_alloc(&resp_arena, data_length, err);
                  if (err != NO_ERROR) {
                    UWU_PANIC(
                        "Fatal: Failed to allocate memory for GOT_MESSAGE "
                        "response!");
                  } else {
                    data[0] = GOT_MESSAGE;
                    data[1] = 1;
                    data[2] = '~';
                    data[3] = message_length;
                    for (size_t i = 0; i < message_length; i++) {
                      data[4 + i] = UWU_String_charAt(&content, i);
                    }

                    UWU_String response = {.data = data, .length = data_length};

                    UWU_PanicIf(
                        pthread_mutex_lock(&UWU_STATE->active_users.mx) != 0,
                        "Fatal: Can't lock the active_users mutex!");
                    broadcast_msg(&response);

                    for (struct UWU_UserListNode *current =
                             UWU_STATE->active_users.start;
                         current != NULL; current = current->next) {

                      if (current->is_sentinel) {
                        continue;
                      }
                      UWU_String current_username = current->data.username;

                      if (UWU_String_equal(&current_username, &conn_username)) {
                        update_last_action(&current->data);

                        if (current->data.status == INACTIVE) {
                          current->data.status = ACTIVE;
                          UWU_String response = create_changed_status_message(
                              &resp_arena, &current->data);
                          broadcast_msg(&response);
                        }
                      }
                    }

                    UWU_PanicIf(
                        pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                        "Fatal: Can't unlock the active_users mutex!");
                  }
                } else {

                  UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->active_users.mx) !=
                                  0,
                              "Fatal: Can't lock the active_users mutex!");
                  UWU_Bool msg_username_exists =
                      UWU_UserList_findByName(&UWU_STATE->active_users,
                                              &msg_username) != NULL;
                  if (!msg_username_exists) {
                    char error[2];
                    error[0] = ERROR;
                    error[1] = USER_NOT_FOUND;

                    UWU_String response = {.data = error, .length = 2};
                    send_msg(c, &response);
                  } else {

                    UWU_String *first = &conn_username;
                    UWU_String *other = &msg_username;

                    if (!UWU_String_firstGoesFirst(first, other)) {
                      first = &msg_username;
                      other = &conn_username;
                    }

                    UWU_String tmp =
                        UWU_String_combineWithOther(first, &SEPARATOR);
                    UWU_String combined =
                        UWU_String_combineWithOther(&tmp, other);
                    UWU_String_freeWithMalloc(&tmp);

                    UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->chats_mx) != 0,
                                "Fatal: Can't lock the chats mutex!");
                    UWU_ChatHistory *history = (UWU_ChatHistory *)hashmap_get(
                        &UWU_STATE->chats, combined.data, combined.length);
                    UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->chats_mx) != 0,
                                "Fatal: Can't unlock the chats mutex!");

                    if (history == NULL) {
                      UWU_PANIC("Fatal: No chat history found for key: %.*s",
                                combined.length, combined.data);
                    } else {

                      UWU_ChatEntry entry = {.content = content,
                                             .origin_username = conn_username};

                      UWU_PanicIf(pthread_mutex_lock(&history->mx) != 0,
                                  "Fatal: Can't lock the chat history mutex "
                                  "for `%.*s`!",
                                  (int)history->channel_name.length,
                                  history->channel_name.data);
                      UWU_ChatHistory_addMessage(history, &entry);
                      UWU_PanicIf(pthread_mutex_unlock(&history->mx) != 0,
                                  "Fatal: Can't unlock the chat history mutex "
                                  "for `%.*s`!",
                                  (int)history->channel_name.length,
                                  history->channel_name.data);

                      size_t data_length =
                          3 + conn_username.length + message_length;
                      char *data =
                          UWU_Arena_alloc(&resp_arena, data_length, err);
                      if (err != NO_ERROR) {
                        UWU_PANIC(
                            "Fatal: Failed to allocate memory for GOT_MESSAGE "
                            "response!");
                      } else {

                        data[0] = GOT_MESSAGE;
                        data[1] = conn_username.length;

                        for (size_t i = 0; i < conn_username.length; i++) {
                          data[2 + i] = UWU_String_charAt(&conn_username, i);
                        }

                        data[2 + conn_username.length] = message_length;
                        for (size_t i = 0; i < message_length; i++) {
                          data[2 + conn_username.length + 1 + i] =
                              UWU_String_charAt(&content, i);
                        }

                        for (struct UWU_UserListNode *current =
                                 UWU_STATE->active_users.start;
                             current != NULL; current = current->next) {

                          if (current->is_sentinel) {
                            continue;
                          }

                          UWU_String current_username = current->data.username;

                          if (UWU_String_equal(&current_username,
                                               &conn_username)) {
                            update_last_action(&current->data);
                          }

                          if (UWU_String_equal(&current_username,
                                               &conn_username) ||
                              UWU_String_equal(&current_username,
                                               &msg_username)) {

                            if (current->data.status == INACTIVE) {
                              current->data.status = ACTIVE;
                              UWU_String response =
                                  create_changed_status_message(&resp_arena,
                                                                &current->data);
                              broadcast_msg(&response);
                            }

                            UWU_String response = {.data = data,
                                                   .length = data_length};
                            send_msg(current->data.conn, &response);
                          }
                        }
                      }
                    }
                  }
                  UWU_PanicIf(
                      pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                      "Fatal: Can't unlock the active_users mutex!");
                }
              }
            } break;

            case GET_MESSAGES: {
              char username_length = msg_data[1];
              if (username_length <= 0) {
                MG_ERROR(("The username is too short!\n"));
              } else {

                UWU_String req_username = {
                    .data = &msg_data[2],
                    .length = username_length,
                };

                if (UWU_String_equal(&req_username, &GROUP_CHAT_CHANNEL)) {
                  size_t max_msg_size = 1 + 1 + 255 * (1 + 255 + 1 + 255);
                  char *data = UWU_Arena_alloc(&resp_arena, max_msg_size, err);

                  if (err != NO_ERROR) {
                    UWU_PANIC(
                        "Fatal: Arena couldn't allocate enough memory for "
                        "message!");
                  } else {
                    UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->group_chat.mx) !=
                                    0,
                                "Fatal: Can't lock the group_chat mutex!");
                    data[0] = GOT_MESSAGES;
                    data[1] = UWU_STATE->group_chat.count;
                    size_t data_length = 2;

                    UWU_ChatHistory_Iterator iter =
                        UWU_ChatHistory_iter(&UWU_STATE->group_chat);
                    for (size_t i = iter.start; i < iter.end; i++) {
                      UWU_ChatEntry entry = UWU_ChatHistory_get(
                          &UWU_STATE->group_chat,
                          i % UWU_STATE->group_chat.capacity);

                      data[data_length] = entry.origin_username.length;
                      data_length++;

                      for (size_t j = 0; j < entry.origin_username.length;
                           j++) {
                        data[data_length] =
                            UWU_String_getChar(&entry.origin_username, j);
                        data_length++;
                      }

                      data[data_length] = entry.content.length;
                      data_length++;

                      for (size_t j = 0; j < entry.content.length; j++) {
                        data[data_length] =
                            UWU_String_getChar(&entry.content, j);
                        data_length++;
                      }
                    }
                    UWU_PanicIf(
                        pthread_mutex_unlock(&UWU_STATE->group_chat.mx) != 0,
                        "Fatal: Can't unlock the group_chat mutex!");

                    UWU_String response = {.data = data, .length = data_length};
                    send_msg(c, &response);
                  }
                } else {
                  UWU_String *first = &req_username;
                  UWU_String *other = &conn_username;

                  if (!UWU_String_firstGoesFirst(first, other)) {
                    first = &conn_username;
                    other = &req_username;
                  }

                  UWU_String tmp =
                      UWU_String_combineWithOther(first, &SEPARATOR);
                  UWU_String combined =
                      UWU_String_combineWithOther(&tmp, other);
                  UWU_String_freeWithMalloc(&tmp);

                  UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->chats_mx) != 0,
                              "Fatal: Can't lock the chats mutex!");
                  UWU_ChatHistory *chat = hashmap_get(
                      &UWU_STATE->chats, combined.data, combined.length);
                  UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->chats_mx) != 0,
                              "Fatal: Can't unlock the chats mutex!");

                  if (NULL == chat) {
                    MG_ERROR(("Can't get chat associated with: %.*s",
                              (int)combined.length, combined.data));
                  } else {
                    size_t max_msg_size = 1 + 1 + 255 * (1 + 255 + 1 + 255);
                    char *data =
                        UWU_Arena_alloc(&resp_arena, max_msg_size, err);

                    if (err != NO_ERROR) {
                      UWU_PANIC(
                          "Fatal: Arena couldn't allocate enough memory for "
                          "message!");
                    } else {
                      data[0] = GOT_MESSAGES;
                      data[1] = chat->count;
                      size_t data_length = 2;

                      UWU_ChatHistory_Iterator iter =
                          UWU_ChatHistory_iter(chat);
                      for (size_t i = iter.start; i < iter.end; i++) {
                        UWU_ChatEntry entry =
                            UWU_ChatHistory_get(chat, i % chat->capacity);

                        data[data_length] = entry.origin_username.length;
                        data_length++;

                        for (size_t j = 0; j < entry.origin_username.length;
                             j++) {
                          data[data_length] =
                              UWU_String_getChar(&entry.origin_username, j);
                          data_length++;
                        }

                        data[data_length] = entry.content.length;
                        data_length++;

                        for (size_t j = 0; j < entry.content.length; j++) {
                          data[data_length] =
                              UWU_String_getChar(&entry.content, j);
                          data_length++;
                        }
                      }

                      UWU_String response = {.data = data,
                                             .length = data_length};
                      send_msg(c, &response);
                    }
                  }
                }
              }

            } break;

            default:
              MG_ERROR(("Unrecognized message!"));
            }
          }
        }
      }
    }

    UWU_Arena_deinit(req_arena);
  }

  UWU_Arena_deinit(resp_arena);
  close(req_reader_fd);
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

    UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->active_users.mx) != 0,
                "Fatal: Can't lock the active_users mutex!");
    {
      UWU_User *user =
          UWU_UserList_findByName(&UWU_STATE->active_users, &source_username);
      if (user != NULL) {
        MG_ERROR(("Can't connect to an already used username!"));
        mg_http_reply(c, 400, "", "INVALID USERNAME");
        UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                    "Fatal: Can't unlock the active_users mutex!");
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
      UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                  "Fatal: Can't unlock the active_users mutex!");
      return;
    }
    MG_INFO(
        ("Currently %d active users!", (int)UWU_STATE->active_users.length));

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
      UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->chats_mx) != 0,
                  "Fatal: Can't lock the chats mutex!");
      if (0 !=
          hashmap_put(&UWU_STATE->chats, combined.data, combined.length, ht)) {
        UWU_PANIC("Fatal: Error creating shared chat for `%.*s`!\n",
                  combined.length, combined.data);
      } else {
        UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->chats_mx) != 0,
                    "Fatal: Can't unlock the chats mutex!");
      }
    }

    c->fn_data = malloc(sizeof(UWU_WSConnInfo));
    {
      if (c->fn_data == NULL) {
        MG_ERROR(("Error: Can't allocate enough memory to create WSConnInfo!"));
        mg_http_reply(c, 500, "", "RAN OUT OF MEMORY");
        UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                    "Fatal: Can't unlock the active_users mutex!");
        return;
      }

      UWU_String copied_username = UWU_String_copy(&source_username, err);
      if (err != NO_ERROR) {
        MG_ERROR(("Error: Can't allocate enough memory to save username!"));
        mg_http_reply(c, 500, "", "RAN OUT OF MEMORY");
        UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                    "Fatal: Can't unlock the active_users mutex!");
        return;
      }

      int pipe_fd[2];
      if (pipe(pipe_fd) < 0) {
        MG_ERROR(("Error: Can't initialize pipe to transmit messages!"));
        mg_http_reply(c, 500, "", "INTERNAL SERVER ERROR");
        UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                    "Fatal: Can't unlock the active_users mutex!");
        return;
      }
      int reader = pipe_fd[0];
      int writer = pipe_fd[1];

      pthread_t pid;
      UWU_ThreadConnInfo *thread_conn_info = malloc(sizeof(UWU_ThreadConnInfo));
      thread_conn_info->username = copied_username;
      thread_conn_info->reader = reader;
      thread_conn_info->c = c;
      pthread_create(&pid, NULL, message_handler, thread_conn_info);

      ((UWU_WSConnInfo *)c->fn_data)->username = copied_username;
      ((UWU_WSConnInfo *)c->fn_data)->writer = writer;
      ((UWU_WSConnInfo *)c->fn_data)->pid = pid;
      if (err != NO_ERROR) {
        MG_ERROR(("Error: Can't allocate enough memory to copy username!"));
        mg_http_reply(c, 500, "", "RAN OUT OF MEMORY");
        UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                    "Fatal: Can't unlock the active_users mutex!");
        return;
      }
    }

    // Tell all other users that a new connection has arrived...
    {
      size_t max_length = 3 + 255;
      char buff[max_length];
      UWU_String msg = changed_status_builder(buff, &user);
      msg.data[0] = REGISTERED_USER;

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
    UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                "Fatal: Can't unlock the active_users mutex!");

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

    const size_t buff_len = 2 + REQ_ARENA_MAX_SIZE;
    char buf[buff_len];
    buf[0] = 1;
    buf[1] = msg_len;

    memcpy(buf + 2, msg_data, msg_len);

    if (-1 == write(conn_info->writer, buf, buff_len)) {
      UWU_PANIC("Fatal: Failed to write msg for connection of `%.*s`",
                (int)conn_info->username.length, conn_info->username.data);
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

    UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->active_users.mx) != 0,
                "Fatal: Can't lock the active_users mutex!");
    UWU_UserList_removeByUsernameIfExists(&UWU_STATE->active_users,
                                          &conn_info->username);
    UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->active_users.mx) != 0,
                "Fatal: Can't unlock the active_users mutex!");

    UWU_PanicIf(pthread_mutex_lock(&UWU_STATE->chats_mx) != 0,
                "Fatal: Can't lock the chats mutex!");
    hashmap_iterate_pairs(&UWU_STATE->chats, remove_if_matches, conn_info);
    UWU_PanicIf(pthread_mutex_unlock(&UWU_STATE->chats_mx) != 0,
                "Fatal: Can't unlock the chats mutex!");

    if (-1 == write(conn_info->writer, EXIT_PTHREAD_MSG, 1)) {
      UWU_PANIC("Fatal: Failed to write msg for connection of `%.*s`",
                (int)conn_info->username.length, conn_info->username.data);
      return;
    }
    pthread_join(conn_info->pid, NULL);
    close(conn_info->writer);

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

  pthread_t timer_shutdown;
  pthread_create(&timer_shutdown, NULL, shutdown_with_time, NULL);

  pthread_t idle_detector_pid;
  pthread_create(&idle_detector_pid, NULL, idle_detector, NULL);

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

  printf("Starting WS listener on %s\n", s_listen_on);
  mg_http_listen(&state.manager, s_listen_on, fn, NULL); // Create HTTP listener
  for (; !UWU_STATE->is_shutting_off;)
    mg_mgr_poll(&state.manager, 1000); // Infinite event loop

  pthread_join(timer_shutdown, NULL);
  pthread_join(idle_detector_pid, NULL);

  // Closes all connections in the server...
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