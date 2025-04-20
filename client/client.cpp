#include "../lib/lib.c"
#include "deps/hashmap/hashmap.h"
#include "mongoose.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QModelIndex>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPainter>
#include <QPushButton>
#include <QRunnable>
#include <QString>
#include <QScreen>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTextEdit>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>

// ***********************************************
// CONSTANTS
// ***********************************************
static const size_t MAX_MESSAGES_PER_CHAT = 100;
static const size_t MAX_CHARACTERS_INPUT = 254;
static const int UNRECOGNIZED_MSG = -1;

// ***********************************************
// UTILS
// ***********************************************

// Copy the data from a mongoose msg and returns it.
// The user is responsable of freeing the data.
UWU_String UWU_String_fromMongoose(mg_str *src, UWU_Err err) {
  UWU_String str = {};

  str.data = (char *)malloc(src->len);

  if (src->buf == NULL) {
    err = MALLOC_FAILED;
    return str;
  }

  memcpy(str.data, src->buf, src->len);
  str.length = src->len;

  return str;
}

// Reads the IP from moongose and stores it in the global variable
// ip if possible.
void store_ip_addr(struct mg_addr *addr, char *buff, size_t buff_size) {
  if (addr->is_ip6) {
    // IPv6
    const char *result = inet_ntop(AF_INET6, addr->ip, buff, buff_size);
    if (result == NULL) {
      UWU_PANIC("Unable to store ip addr v6");
    }
  } else {
    // IPv4
    const char *result = inet_ntop(AF_INET, addr->ip, buff, buff_size);
    if (result == NULL) {
      UWU_PANIC("Unable to store ip addr v6");
    }
  }

  printf("Stored IP: %s\n", buff);
  printf("Port: %u\n", ntohs(addr->port));
}

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

int remove_all(void *context, struct hashmap_element_s *const e) {
  UWU_ChatHistory *data = (UWU_ChatHistory *)e->data;

  UWU_ChatHistory_deinit(data);
  free(data);
  return -1;
}

int remove_if_matches(void *context, struct hashmap_element_s *const e) {
  UWU_String *user_name = (UWU_String *)context;
  UWU_String hash_key = {
      .data = (char *)e->key,
      .length = e->key_len,
  };

  if (UWU_String_equal(user_name, &hash_key)) {
    UWU_ChatHistory *data = (UWU_ChatHistory *)e->data;

    UWU_ChatHistory_deinit(data);
    free(data);
    return -1;
  }

  return 0;
}

// Does not copy the user data.
void register_user(UWU_User *user, UWU_UserList *userList, hashmap_s *chats) {

  UWU_Err err = NO_ERROR;

  UWU_UserListNode userNode = UWU_UserListNode_newWithValue(*user);
  UWU_UserList_insertEnd(userList, &userNode, err);
  if (err != NO_ERROR) {
    UWU_PANIC("Fatal: Failed to insert Group chat to the UserCollection!\n");
  }

  UWU_ChatHistory *userChat =
      (UWU_ChatHistory *)malloc(sizeof(UWU_ChatHistory));
  if (userChat == NULL) {
    UWU_PANIC("Fatal: Failed to allocate memory for groupal chat history\n");
  }
  *userChat = UWU_ChatHistory_init(MAX_MESSAGES_PER_CHAT, user->username, err);

  if (0 != hashmap_put(chats, user->username.data, user->username.length,
                       userChat)) {
    UWU_PANIC("Fatal: Error creating a chat entry for the group chat.");
  }
};

void unregister_user(UWU_User *user, UWU_UserList *userList, hashmap_s *chats) {

  hashmap_iterate_pairs(chats, remove_if_matches, &user->username);

  UWU_UserList_removeByUsernameIfExists(userList, &user->username);
};

// ***********************************************
// MODEL
// ***********************************************

struct UWU_ClientState {
  UWU_User CurrentUser;

  // Stores the users the client can messages to.
  UWU_UserList ActiveUsers;
  // A pointer to the current selected chat, set to NULL if no client is
  // selected.
  UWU_ChatHistory *currentChat;
  // Group chat user
  // UWU_User GroupChat;
  // Saves all the chat histories.
  // Key: The name of the user this client chat chat to.
  // Value: An UWU_History item.
  struct hashmap_s Chats;
};

UWU_ClientState *UWU_STATE = NULL;

// Connection to the websocket server
static mg_connection *ws_conn;

static char ip[128] = {};

static char *s_url;
static const char *s_ca_path = "ca.pem";

UWU_ClientState init_ClientState(char *username, char *url, UWU_Err err) {
  UWU_ClientState state = UWU_ClientState{};

  // SET URL
  // Calculate required length for the final URL
  int needed = snprintf(NULL, 0, "%s?name=%s", url, username);
  printf("size %d needed\n", needed);
  if (needed < 0) {
    UWU_PANIC("Could not calculate connection URL size.\n");
    err = MALLOC_FAILED;
    return state;
  }

  // Allocate memory (+1 for null terminator)
  s_url = (char *)malloc(needed + 1);
  if (s_url == NULL) {
    UWU_PANIC("Memory allocation failed.\n");
    err = MALLOC_FAILED;
    return state;
  }

  // Build the actual URL
  snprintf(s_url, needed + 1, "%s?name=%s", url, username);

  // Print for debug
  printf("Username: %s\n", username);
  printf("Final connection URL: %s\n", s_url);

  // CREATE USER
  size_t name_length = strlen(username);
  char *username_data = (char *)malloc(name_length);
  if (NULL == username_data) {
    err = MALLOC_FAILED;
    return state;
  }
  strcpy(username_data, username);
  UWU_String current_username = {.data = username_data, .length = name_length};

  // User initialization
  state.CurrentUser.username = current_username;
  state.CurrentUser.status = ACTIVE;

  // CREATE USER LIST
  state.ActiveUsers = UWU_UserList_init(err);
  if (err != NO_ERROR) {
    return state;
  }

  // CREATE CHATS
  if (0 != hashmap_create(8, &state.Chats)) {
    err = HASHMAP_INITIALIZATION_ERROR;
    return state;
  }

  // CREATE GROUP USER
  char *group_chat_name = (char *)malloc(sizeof(char));
  if (group_chat_name == NULL) {
    err = MALLOC_FAILED;
    return state;
  }
  *group_chat_name = '~';
  UWU_String group_name = {.data = group_chat_name, .length = 1};
  UWU_User group_user = {.username = group_name, .status = ACTIVE};

  register_user(&group_user, &state.ActiveUsers, &state.Chats);

  // SET CURRENT CHAT
  state.currentChat = NULL;

  UWU_String_freeWithMalloc(&group_name);

  return state;
}

void deinit_ClientState(UWU_ClientState *state) {
  MG_INFO(("Cleaning current chat pointer..."));
  state->currentChat = NULL;

  MG_INFO(("Cleaning DM Chat histories..."));
  hashmap_destroy(&state->Chats);

  MG_INFO(("Cleaning Current User"));
  UWU_String_freeWithMalloc(&state->CurrentUser.username);

  MG_INFO(("Cleaning User List..."));
  UWU_UserList_deinit(&state->ActiveUsers);

  MG_INFO(("Cleaning connection url..."));
  free(s_url);
}

// *******************************************
// CONTROLLER
// *******************************************

void get_messages_handler(UWU_String *contact);
void send_message_handler(UWU_String *user_recipient, UWU_String *text);
void list_users_handler();
void change_status_handler(UWU_ConnStatus status);

// Object responsable for handling a message from the WS.
// One is created per message from tehe server.
class MessageTask : public QObject, public QRunnable {
  Q_OBJECT
public:
  MessageTask(UWU_String s) : msg(s) {}

  // Logic to handle message processing.
  void run() override {
    print_msg(&msg, "Client", "GOT MSG:");

    QMutexLocker locker(&mutex);
    switch (msg.data[0]) {
    case ERROR: {
      int err_code = (int)msg.data[1];
      emit errorMsg(err_code);
    } break;
    case LISTED_USERS: {
      // WARNING: This message will basically reset the UWU_STATE first before
      // adding anything else.
      UWU_Err err = NO_ERROR;

      // DESTROYING OLD USER LIST AND CHATS.

      // UNCOMMENT TO SHOW BUG
      // hashmap_iterate_pairs(&UWU_STATE->Chats, remove_all, NULL); //
      hashmap_destroy(&UWU_STATE->Chats);
      UWU_UserList_deinit(&UWU_STATE->ActiveUsers);

      // CREATE USER LIST
      UWU_STATE->ActiveUsers = UWU_UserList_init(err);
      if (err != NO_ERROR) {
        UWU_PANIC("Unable to allocate space for new user list");
      }

      // CREATE CHATS
      if (0 != hashmap_create(8, &UWU_STATE->Chats)) {
        UWU_PANIC("Unable to create chat list hashmap");
      }

      // ADDING GROUP CHAT
      char *group_chat_name = (char *)malloc(sizeof(char));
      if (group_chat_name == NULL) {
        UWU_PANIC("Unable allocate space for group chat user");
      }
      *group_chat_name = '~';
      UWU_String group_name = {.data = group_chat_name, .length = 1};
      UWU_User group_user = {.username = group_name, .status = ACTIVE};

      register_user(&group_user, &UWU_STATE->ActiveUsers, &UWU_STATE->Chats);

      // INSERTING OTHER USERS
      const int totalUsers = msg.data[1];
      int lenUsernames = 0;
      for (int i = 0; i < totalUsers; i++) {
        int index = 2 + i + lenUsernames;
        int usernameLen = msg.data[index];
        char *usernameValue = (char *)malloc(usernameLen);
        if (usernameValue == NULL) {
          UWU_PANIC("Failed to allocate space for new user");
        }
        for (int j = 0; j < usernameLen; j++) {
          int usernameIndex = index + 1;
          usernameValue[j] = msg.data[usernameIndex + j];
        }
        UWU_ConnStatus userStatus =
            (UWU_ConnStatus)msg.data[index + usernameLen + 1];

        UWU_String usernameStr = {.data = usernameValue, .length = usernameLen};
        UWU_User user = {.username = usernameStr, .status = userStatus};

        // Ignore our own username.
        if (UWU_String_equal(&UWU_STATE->CurrentUser.username, &usernameStr)) {
          free(usernameValue);
          lenUsernames += usernameLen + 1;
          continue;
        }

        printf("INSERTING %.*s\n", usernameLen, usernameValue);
        register_user(&user, &UWU_STATE->ActiveUsers, &UWU_STATE->Chats);

        lenUsernames += usernameLen + 1;
      }

      printf("totalUsers: %d\n", UWU_STATE->ActiveUsers.length);
    } break;
    case GOT_USER:
      /* code */
      break;
    case REGISTERED_USER: {
      UWU_Err err = NO_ERROR;
      size_t username_length = msg.data[1];
      UWU_ConnStatus req_status = (UWU_ConnStatus)msg.data[2 + username_length];
      UWU_String req_username = {.data = &msg.data[2],
                                 .length = username_length};
      UWU_User *temp =
          UWU_UserList_findByName(&UWU_STATE->ActiveUsers, &req_username);

      if (temp == NULL) {
        UWU_String usernameStr = UWU_String_copy(&req_username, err);
        if (err != NO_ERROR) {
          UWU_PANIC("Unable to register new user");
        }
        UWU_User user = UWU_User{.username = usernameStr, .status = req_status};

        register_user(&user, &UWU_STATE->ActiveUsers, &UWU_STATE->Chats);
        printf("INSERTING %.*s\n", username_length, req_username.data);
      }
    } break;
    case CHANGED_STATUS: {
      size_t username_length = msg.data[1];
      UWU_ConnStatus req_status = (UWU_ConnStatus)msg.data[2 + username_length];
      UWU_String req_username = {.data = &msg.data[2],
                                 .length = username_length};
      if (UWU_String_equal(&UWU_STATE->CurrentUser.username, &req_username)) {
        UWU_STATE->CurrentUser.status = req_status;
        printf("CHANGING %.*s STATUS TO : %d\n",
               UWU_STATE->CurrentUser.username.length,
               UWU_STATE->CurrentUser.username.data, (int)req_status);
      } else {
        UWU_Bool found_it = FALSE;
        UWU_Bool should_delete = FALSE;
        UWU_User *userToDelete = NULL;
        for (struct UWU_UserListNode *current = UWU_STATE->ActiveUsers.start;
             current != NULL; current = current->next) {
          if (current->is_sentinel) {
            continue;
          }

          if (UWU_String_equal(&current->data.username, &req_username)) {
            found_it = TRUE;
            if (req_status == DISCONNETED) {
              should_delete = TRUE;
              userToDelete = &current->data;
            } else {
              current->data.status = req_status;
              printf("CHANGING %.*s STATUS TO : %d\n",
                     current->data.username.length, current->data.username.data,
                     (int)req_status);
            }
          }
        }
        if (should_delete) {
          printf("DELETING %.*s \n", userToDelete->username.length,
                 userToDelete->username.data);
          unregister_user(userToDelete, &UWU_STATE->ActiveUsers,
                          &UWU_STATE->Chats);

          printf("User removed, totalUsers: %d\n",
                 UWU_STATE->ActiveUsers.length);
        } else if (!found_it) {
          UWU_Err err = NO_ERROR;
          UWU_User user = {.username = req_username, .status = req_status};
          struct UWU_UserListNode node = UWU_UserListNode_newWithValue(user);

          UWU_UserList_insertEnd(&UWU_STATE->ActiveUsers, &node, err);
          if (err != NO_ERROR) {
            UWU_PANIC("Fatal: Couldn't add username to active usernames!");
            return;
          }
        }
      }
    } break;
    case GOT_MESSAGE: {
      if (UWU_STATE->CurrentUser.status == BUSY) {
        break;
      }
      if (UWU_STATE->currentChat == NULL) {
        printf("No chat is selected to append the_ new message\n");
        break;
      }

      UWU_Err err = NO_ERROR;
      size_t username_length = msg.data[1];
      size_t msg_length = msg.data[2 + username_length];
      char *username = (char *)malloc(username_length);
      char *chat = (char *)malloc(msg_length);

      if (username == NULL || chat == NULL) {
        UWU_PANIC("Unable to allocate memory for new incoming messages");
      }

      for (size_t i = 0; i < username_length; i++) {
        username[i] = msg.data[2 + i];
      }

      for (size_t i = 0; i < msg_length; i++) {
        chat[i] = msg.data[2 + username_length + 1 + i];
      }

      UWU_String contact = {.data = username, .length = username_length};
      UWU_String content = {.data = chat, .length = msg_length};

      UWU_ChatEntry entry =
          UWU_ChatEntry{.content = content, .origin_username = contact};

      UWU_ChatHistory *history = NULL;

      if (UWU_String_equal(&UWU_STATE->CurrentUser.username, &contact)) {
        // If its from the current user append it to the current chat.
        history = UWU_STATE->currentChat;
      } else {
        // Search for the contact chat.
        history = (UWU_ChatHistory *)hashmap_get(&UWU_STATE->Chats,
                                                 contact.data, contact.length);
      }
      if (history == NULL) {
        printf("No matched entry to store the incoming msg. Dismising");
      } else {
        UWU_ChatHistory_addMessage(history, &entry);
      }

      free(username);
      free(chat);
    } break;
    case GOT_MESSAGES: {
      if (UWU_STATE->CurrentUser.status == BUSY) {
        break;
      }
      if (UWU_STATE->currentChat == NULL) {
        printf("No chat is selected to append the new messages\n");
        break;
      }
      // Clearing messages.
      UWU_ChatHistory_clear(UWU_STATE->currentChat);

      UWU_ChatHistory *history = UWU_STATE->currentChat;

      int totalMessages = msg.data[1];
      int offset = 0;
      for (size_t n = 0; n < totalMessages; n++) {
        size_t index = 2 + offset;
        size_t lenUsername = msg.data[index];
        size_t lenChat = msg.data[index + lenUsername + 1];
        char *username = (char *)malloc(lenUsername);
        char *chat = (char *)malloc(lenChat);
        if (username == NULL || chat == NULL) {
          UWU_PANIC("Unable to allocate memory for new incoming messages");
        }

        for (size_t i = 0; i < lenUsername; i++) {
          username[i] = msg.data[index + 1 + i];
        }

        for (size_t i = 0; i < lenChat; i++) {
          chat[i] = msg.data[index + lenUsername + 2 + i];
        }

        UWU_String content = UWU_String{.data = chat, .length = lenChat};
        UWU_String contact =
            UWU_String{.data = username, .length = lenUsername};

        UWU_ChatEntry entry =
            UWU_ChatEntry{.content = content, .origin_username = contact};

        UWU_ChatHistory_addMessage(history, &entry);

        free(username);
        free(chat);

        offset = offset + lenUsername + lenChat + 2;
      }

    } break;
    default:
      fprintf(stderr, "Error: Unrecognized message from server!\n");
      emit errorMsg(UNRECOGNIZED_MSG);
      break;
    }
    UWU_String_freeWithMalloc(&msg);

    emit msgPrococessed();
  }

private:
  // Message to process.
  UWU_String msg;
  // Shared mutex across all MessageTask instances for syncronization.
  static QMutex mutex;

signals:
  // Signal sent when a message is done processing.
  void msgPrococessed();
  // Sent when received an error from the server.
  void errorMsg(int err);
};
// Mutex initialization
QMutex MessageTask::mutex;

// Object for controlling the Websocket connection,
// message receiving and update global state: UWU_STATE.
// One can say it's the core logic of the app.
class Controller : public QObject {
  Q_OBJECT
  struct mg_mgr mgr;
  bool done = false;
  QLabel *ipLabel = nullptr;

public slots:

  void setIpLabel(QLabel *label) { ipLabel = label; }

  void updateIpLabel() {
    if (ipLabel != nullptr) {
      ipLabel->setText(QString::fromUtf8(ip));
    }
  }
  // Cleaning function to call when window is closed
  void stop() {
    printf("\nClosing websocket connection...\n");
    mg_mgr_free(&mgr);
  }

  static void ws_listener(struct mg_connection *c, int ev, void *ev_data) {
    Controller *controller = (Controller *)c->fn_data;
    if (ev == MG_EV_OPEN) {
      c->is_hexdumping = 1;
    } else if (ev == MG_EV_CLOSE) {
      printf("Closing connection\n");
      printf("This may happened due if the client was unable to connect to the "
             "server.\n");
      emit controller->clientDisconnected();
    } else if (ev == MG_EV_CONNECT && mg_url_is_ssl(s_url)) {
      // On connection established
      struct mg_tls_opts opts = {.name = mg_url_host(s_url)};
      mg_tls_init(c, &opts);
    } else if (ev == MG_EV_ERROR) {
      // On error, log error message
      MG_ERROR(("%p %s", c->fd, (char *)ev_data));
    } else if (ev == MG_EV_WS_OPEN) {
      // When websocket handshake is successful, send message
      store_ip_addr(&c->loc, ip, sizeof(ip));

      // Add QT controller for updating IpLabel
      controller->updateIpLabel();

      list_users_handler();
    } else if (ev == MG_EV_WS_MSG) {
      // Copying message
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
      UWU_Err err = NO_ERROR;
      UWU_String msg = UWU_String_fromMongoose(&wm->data, err);

      // Launching thread
      MessageTask *task = new MessageTask(msg);
      QObject::connect(task, &MessageTask::msgPrococessed, controller,
                       &Controller::onMsgProcessed, Qt::QueuedConnection);
      QObject::connect(task, &MessageTask::errorMsg, controller,
                       &Controller::onErrorMsg, Qt::QueuedConnection);
      QThreadPool::globalInstance()->start(task);
    }
  }

  // Starts the controller
  void run() {
    printf("Starting Websocket controller...");

    // Initialise event manager
    mg_mgr_init(&mgr);
    // Create client
    ws_conn = mg_ws_connect(&mgr, s_url, ws_listener, this, NULL);
    mg_log_set(1); // Disable debu logging.

    // WS Event loop
    QTimer *pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this,
            [=]() { mg_mgr_poll(&mgr, 100); });
    pollTimer->start(50);
  }

  // Called when a TaskMessage has done its work.
  // It instantly emits a signal to announce UWU state has changed.
  void onMsgProcessed() {
    printf("MSG PROCESSED...\n");
    emit stateChanged(UWU_STATE);
  }

  void onErrorMsg(int err) {
    switch (err) {
    case USER_NOT_FOUND:
      printf("ERROR: User not found\n");
      emit userNotFound();
      break;
    case INVALID_STATUS:
      printf("ERROR: Invalid status\n");
      emit invalidStatus();
      break;
    case EMPTY_MESSAGE:
      printf("ERROR: Empty message\n");
      emit emptyMessage(3000);
      break;
    case USER_ALREADY_DISCONNECTED:
      printf("ERROR: User already disconnected\n");
      emit userAlreadyDisconnected();
      break;
    default:
      printf("ERROR: Unrecognized message\n");
      emit gotInvalidMessage();
      break;
    }
  }

signals:
  // Signal to announce UWU_STATE has changed.
  void stateChanged(UWU_ClientState *newState);
  void finished();
  // ERRORS
  // The connection to the server was cut unexpectedely.
  void clientDisconnected();
  // The message received from the server does not figure out on the protocol
  void gotInvalidMessage();
  // The user you tried to access doesn't exist!
  void userNotFound();
  // The status you want to change to doesn't exist!
  void invalidStatus();
  // The message you wish to send is empty!
  void emptyMessage(int);
  // You're trying to communicate with a disconnected user!
  void userAlreadyDisconnected();
};

// HANDLERS
// ****************************

// Fetch list of users
void list_users_handler() {
  char data[1] = {1};
  mg_ws_send(ws_conn, data, 1, WEBSOCKET_OP_BINARY);
  printf("LIST USERS!\n");
}

// NOTE: MIGHT NOT BE USEFUL FOR OUR PURPOSES
void get_user_handler() {
  if (UWU_STATE == NULL) {
    return;
  }
}

// Change status of the current user
void change_status_handler(UWU_ConnStatus status) {
  if (UWU_STATE == NULL) {
    return;
  }

  UWU_User *currentUser = &UWU_STATE->CurrentUser;
  size_t username_length = currentUser->username.length;
  size_t length = 3 + username_length;
  char *data = (char *)malloc(length);
  if (data == NULL) {
    UWU_PANIC("Could not allocate memory to set STATUS BUSY");
  }

  data[0] = CHANGE_STATUS;
  data[1] = username_length;

  for (size_t i = 0; i < username_length; i++) {
    data[2 + i] = UWU_String_charAt(&currentUser->username, i);
  }

  data[length - 1] = status;

  mg_ws_send(ws_conn, data, length, WEBSOCKET_OP_BINARY);
  free(data);
}

// Fetch messages for the current chat history selected
void get_messages_handler(UWU_String *contact) {
  if (UWU_STATE == NULL) {
    return;
  }
  size_t length = 2 + contact->length;
  char *data = (char *)malloc(length);
  if (data == NULL) {
    UWU_PANIC("Could not allocate memory to set STATUS BUSY");
  }

  data[0] = GET_MESSAGES;
  data[1] = contact->length;

  for (size_t i = 0; i < contact->length; i++) {
    data[2 + i] = UWU_String_charAt(contact, i);
  }

  mg_ws_send(ws_conn, data, length, WEBSOCKET_OP_BINARY);
  free(data);
}

// Sends a chat message with given history
void send_message_handler(UWU_String *user_recipient, UWU_String *text) {
  size_t username_length = user_recipient->length;
  size_t message_length = text->length;
  size_t length = 3 + username_length + message_length;
  printf("Total length: %d\n", length);
  printf("username lenght: %d\n", username_length);
  printf("message lenght: %d\n", message_length);
  char *data = (char *)malloc(length);

  if (data == NULL) {
    UWU_PANIC("Unable to allocate memory for sending message");
  }

  data[0] = SEND_MESSAGE;
  data[1] = username_length;
  for (size_t i = 0; i < username_length; i++) {
    data[2 + i] = UWU_String_charAt(user_recipient, i);
  }
  // a | a | a a a | a | a
  // 0 | 1 | 2 3 4 | 5 | 6
  // 1   2   3 4 5 | 5 | 7
  data[2 + username_length] = message_length;
  for (size_t i = 0; i < message_length; i++) {
    data[3 + username_length + i] = UWU_String_charAt(text, i);
  }

  UWU_String a = UWU_String{.data = data, .length = length};
  print_msg(&a, "CLIENT", "SENT");

  mg_ws_send(ws_conn, data, length, WEBSOCKET_OP_BINARY);

  free(data);
}

// *******************************************
// VIEW
// *******************************************

// class for creating the window project
class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
    setWindowTitle("CHAD GUI");
  }

protected:
  // In this function you can modify what happens when closing the chat
  void closeEvent(QCloseEvent *event) override {
    printf("CLEANING UP... \n");
    deinit_ClientState(UWU_STATE);
    event->accept();
  }
};

// Class for the status button
class StatusButton : public QPushButton {
  Q_OBJECT
public:
  StatusButton(UWU_ConnStatus &RefStatus, Controller *controller,
               QWidget *parent = nullptr)
      : QPushButton(parent), status(ACTIVE), externalStatus(RefStatus) {
    status = externalStatus;

    setMaximumWidth(50);
    setContentsMargins(0, 0, 0, 100);

    connect(controller, &Controller::stateChanged, this,
            &StatusButton::updateStatus);
    connect(this, &QPushButton::clicked, this, &StatusButton::clickSlot);
  }

public slots:
  // Change to opposite status set status true for Busy
  void clickSlot() {
    if (status == ACTIVE) {
      status = BUSY;
    } else if (status == BUSY) {
      status = ACTIVE;
    } else if (status == INACTIVE) {
      status = BUSY;
    }

    externalStatus = status;

    change_status_handler(status);
    update();
  }

  void updateStatus(UWU_ClientState *newStatus) {
    status = newStatus->CurrentUser.status;
    externalStatus = status;
    update();
  }

protected:
  void paintEvent(QPaintEvent *event) override {
    QPushButton::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor circleColor;

    if (status == ACTIVE) {
      circleColor = QColor(0, 0, 255);
    } else if (status == BUSY) {
      circleColor = QColor(255, 0, 0);
    } else {
      circleColor = QColor(255, 255, 0);
    }
    painter.setBrush(circleColor);
    painter.setPen(Qt::NoPen);

    int radius = 10;
    int centerX = width() / 2;
    int centerY = height() / 2;

    painter.drawEllipse(QPoint(centerX, centerY), radius, radius);
  }

  // Class attributes
private:
  UWU_ConnStatus status;
  UWU_ConnStatus &externalStatus;
  Controller *controller;
};

class UWUUserQT : public QWidget {
  Q_OBJECT
public:
  UWUUserQT(const char username[], const char ip[], QWidget *parent = nullptr)
      : QWidget(parent) {

    std::strncpy(username_, username, sizeof(username_) - 1);

    username_[sizeof(username_) - 1] = '\0';

    std::strncpy(ip_, ip, sizeof(ip_) - 1);
    ip_[sizeof(ip_) - 1] = '\0';

    QPalette UWUUSerQTPallete = this->palette();
    UWUUSerQTPallete.setColor(QPalette::Window, QColor(189, 189, 189));
    this->setAutoFillBackground(true);
    this->setPalette(UWUUSerQTPallete);

    QVBoxLayout *verticalLayout = new QVBoxLayout(this);
    verticalLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *nameLabel = new QLabel(username);
    QLabel *ipLabel = new QLabel(ip);

    nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ipLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    verticalLayout->addWidget(nameLabel);
    verticalLayout->addWidget(ipLabel);

    setStyleSheet("padding: 5px");
  }

protected:
  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      qDebug() << "User:" << username_;
    }
    QWidget::mousePressEvent(event);
  }

private:
  char username_[255];
  char ip_[255];
};

class UWUUserModel : public QAbstractListModel {
  Q_OBJECT
public:
  enum UserRoles { UsernameRole = Qt::UserRole + 1, IpRole = Qt::UserRole + 2 };

  UWUUserModel(UWU_ClientState &state, QObject *parent = nullptr)
      : QAbstractListModel(parent), clientState(state) {}

  int rowCount(const QModelIndex &parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : clientState.ActiveUsers.length;
  }

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= clientState.ActiveUsers.length) {
      return QVariant();
    }

    const UWU_UserListNode *node = getNodeAt(index.row());
    if (!node || node->is_sentinel) {
      return QVariant();
    }

    const UWU_User &user = node->data;

    if (role == Qt::DisplayRole) {
      return QString::fromUtf8(user.username.data);
    } else if (role == UsernameRole) {
      return QString::fromUtf8(user.username.data);
    } else if (role == IpRole) {
      return QString::fromUtf8("fixme");
    }
    return QVariant();
  }

public slots:
  void refreshUserList() {
    beginResetModel();
    endResetModel();
  }

private:
  UWU_ClientState &clientState;

  const UWU_UserListNode *getNodeAt(int index) const {
    const UWU_UserListNode *node = clientState.ActiveUsers.start;
    int currentIndex = 0;

    while (node != nullptr && currentIndex <= index) {
      if (!node->is_sentinel) {
        if (currentIndex == index) {
          return node;
        }
        ++currentIndex;
      }
      node = node->next;
    }

    return nullptr;
  }
};

class UWUUserDelegate : public QStyledItemDelegate {
  Q_OBJECT
public:
  UWUUserDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    if (!index.isValid())
      return;

    QString username = index.data(UWUUserModel::UsernameRole).toString();
    QString ip = index.data(UWUUserModel::IpRole).toString();

    // Colores según hover
    QColor bgColor;

    if (option.state & QStyle::State_Selected) {
      bgColor = QColor(31, 181, 25); // selected
    } else if (option.state & QStyle::State_MouseOver) {
      bgColor = QColor(83, 247, 72); // (hover)
    } else {
      bgColor = QColor(189, 189, 189); // base
    }
    QColor textColor = Qt::black;

    painter->save();
    painter->fillRect(option.rect, bgColor);

    painter->setPen(textColor);
    QFont font = option.font;
    font.setBold(true);
    painter->setFont(font);

    QRect nameRect = option.rect.adjusted(5, 5, -5, -option.rect.height() / 2);
    QRect ipRect = option.rect.adjusted(5, option.rect.height() / 2, -5, -5);

    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, username);
    painter->setFont(option.font); // IP sin negrita
    painter->drawText(ipRect, Qt::AlignLeft | Qt::AlignVCenter, ip);

    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override {
    UWUUserQT tempWidget("dummy", "dummy");
    return QSize(option.rect.width(), tempWidget.sizeHint().height());
  }
};

class ChatLineEdit : public QLineEdit {
  Q_OBJECT
public:
  ChatLineEdit(QString *msg, QWidget *parent = nullptr)
      : QLineEdit(parent), message(msg) {
    setPlaceholderText("Write a message");
    connect(this, &QLineEdit::textChanged, this, &ChatLineEdit::onTextChanged);
  }

private slots:
  void onTextChanged(const QString &text) {
    if (message) {
      *message = text;
    }
  }

private:
  QString *message;
};

class ChatSendButton : public QPushButton {
  Q_OBJECT
public:
  ChatSendButton(QString *msg, ChatLineEdit *input, QString *selectedUser,
                 QWidget *parent = nullptr)
      : QPushButton(parent), message(msg), inputField(input),
        selectedUser(selectedUser) {
    setMaximumWidth(100);
    setIcon(QIcon("icons/send-icond.png"));
    connect(this, &QPushButton::clicked, this, &ChatSendButton::onSendClicked);
  }

private slots:
  void onSendClicked() {
    if (message && selectedUser) {
      UWU_String *UWU_SelectedUser = (UWU_String *)malloc(sizeof(UWU_String));
      UWU_String *UWU_Message = (UWU_String *)malloc(sizeof(UWU_String));

      QByteArray usernameUtf8 = selectedUser->toUtf8();
      char *copiedUsername = (char *)malloc(usernameUtf8.size());
      memcpy(copiedUsername, usernameUtf8.constData(), usernameUtf8.size());

      UWU_SelectedUser->data = copiedUsername;
      UWU_SelectedUser->length = usernameUtf8.size();

      QByteArray messageUtf8 = message->toUtf8();
      char *copiedMessage = (char *)malloc(messageUtf8.size());
      memcpy(copiedMessage, messageUtf8.constData(), messageUtf8.size());

      UWU_Message->data = copiedMessage;
      UWU_Message->length = messageUtf8.size();

      send_message_handler(UWU_SelectedUser, UWU_Message);
      *message = "";

      free(UWU_SelectedUser->data);
      free(UWU_SelectedUser);
      free(UWU_Message->data);
      free(UWU_Message);
    }
    if (inputField) {
      inputField->clear();
    }
  }

private:
  QString *message;
  ChatLineEdit *inputField;
  QString *selectedUser;
};

class UWUMessageModel : public QAbstractListModel {
  Q_OBJECT

public:
  UWUMessageModel(QObject *parent = nullptr)
      : QAbstractListModel(parent), chatHistory(nullptr) {}

  void setChatHistory(UWU_ChatHistory *history) {
    beginResetModel();
    chatHistory = history;
    endResetModel();
  }

  int rowCount(const QModelIndex &parent = QModelIndex()) const override {
    if (!chatHistory || parent.isValid())
      return 0;
    return chatHistory->count;
  }

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override {
    if (!chatHistory || !index.isValid() ||
        index.row() >= (int)chatHistory->count)
      return QVariant();

    if (role == Qt::DisplayRole) {
      UWU_ChatEntry entry = chatHistory->messages[index.row()];
      QString sender = QString::fromUtf8(entry.origin_username.data,
                                         entry.origin_username.length);
      QString content =
          QString::fromUtf8(entry.content.data, entry.content.length);

      qDebug() << "Message #" << index.row();
      qDebug() << "Sender raw:"
               << QByteArray(entry.origin_username.data,
                             entry.origin_username.length);
      qDebug() << "Content raw:"
               << QByteArray(entry.content.data, entry.content.length);

      return QString("%1: %2").arg(sender, content);
    }

    return QVariant();
  }

private:
  UWU_ChatHistory *chatHistory;
};

class Toast : public QLabel {
  public:
      Toast(const QString& text, QWidget* parent = nullptr, Qt::WindowFlags f = Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint)
          : QLabel(text, parent, f) {
          setAlignment(Qt::AlignCenter);
          setStyleSheet("background-color: rgba(0, 0, 0, 150); color: white; padding: 10px; border-radius: 5px; font-size: 100px");
          setAttribute(Qt::WA_TranslucentBackground);
          hide();
      }
  
      void showToast(int duration) {
          if (isVisible()) {
              hide();
          }
  
          if (parentWidget()) { 
            QWidget* parent = parentWidget();
            QPoint parentCenter = parent->geometry().center();
            int x = parentCenter.x() - width() / 2;
            int y = parentCenter.y() - height() / 2 - 50; 
            move(x, y);
        } else {
            QScreen* screen = QGuiApplication::primaryScreen();
            if (screen) {
                QRect screenGeometry = screen->geometry();
                int x = (screenGeometry.width() - width()) / 2;
                int y = screenGeometry.height() / 4;
                move(x, y);
            }
        }
  
          show();
          setGraphicsEffect(new QGraphicsOpacityEffect(this));
          QPropertyAnimation* animation = new QPropertyAnimation(graphicsEffect(), "opacity");
          animation->setDuration(500);
          animation->setStartValue(0.0);
          animation->setEndValue(1.0);
          animation->start(QAbstractAnimation::DeleteWhenStopped);
  
          QTimer::singleShot(duration, this, [this]() {
              QPropertyAnimation* animation = new QPropertyAnimation(graphicsEffect(), "opacity");
              animation->setDuration(500);
              animation->setStartValue(1.0);
              animation->setEndValue(0.0);
              animation->start(QAbstractAnimation::DeleteWhenStopped);
              connect(animation, &QPropertyAnimation::finished, this, &Toast::hide);
          });
      }
  };

#include "client.moc"

int main(int argc, char *argv[]) {

  // GLOBAL STATE INITIALIZATION
  // ***************************

  if (argc != 3) {
    fprintf(stderr, "Usage: %s <Username> <WebSocket_URL>\n", argv[0]);
    return 1;
  }

  if (strlen(argv[1]) > 255) {
    UWU_PANIC("Username to large!...");
    return 1;
  }

  // Get the username from the command line arguments
  char *username = argv[1];
  // Get the WebSocket URL from the command line arguments
  char *ws_url = argv[2];

  UWU_Err err = NO_ERROR;

  UWU_ClientState state = init_ClientState(username, ws_url, err);
  if (err != NO_ERROR) {
    UWU_PANIC("Unable to client state...");
    return 1;
  }
  UWU_STATE = &state;

  QApplication app(argc, argv);

  // CONTROLLER AND WS CLIENT CONNECTION INITALIZAITON
  // **********************

  QThread *thread = new QThread;
  Controller *controller = new Controller;
  controller->moveToThread(thread);

  QObject::connect(thread, &QThread::started, controller, &Controller::run);
  QObject::connect(controller, &Controller::finished, thread, &QThread::quit);
  QObject::connect(controller, &Controller::finished, controller,
                   &Controller::deleteLater);
  QObject::connect(controller, &Controller::finished, &app,
                   &QCoreApplication::quit);
  QObject::connect(thread, &QThread::finished, thread, &QThread::deleteLater);
  QObject::connect(&app, &QCoreApplication::aboutToQuit,
                   [controller]() { controller->stop(); });

  // Start the thread
  thread->start();

  // LAYOUT INITIALIZATION
  // **********************

  // Crear la ventana principal
  MainWindow mainWindow;

  UWUUserModel *userModel = new UWUUserModel(state);
  QListView *chatUsers = new QListView();
  chatUsers->setModel(userModel);
  chatUsers->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  chatUsers->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  chatUsers->viewport()->setAttribute(Qt::WA_Hover);
  chatUsers->setAttribute(Qt::WA_Hover);
  chatUsers->setSelectionMode(QAbstractItemView::SingleSelection);
  chatUsers->setSelectionBehavior(QAbstractItemView::SelectRows);
  chatUsers->setMouseTracking(true);
  chatUsers->viewport()->setMouseTracking(true);

  UWUMessageModel *messageModel = new UWUMessageModel();
  QListView *chatMessages = new QListView();
  chatMessages->setModel(messageModel);
  chatMessages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  QObject::connect(
      controller, &Controller::stateChanged, [&](UWU_ClientState *newState) {
        userModel->refreshUserList();
        if (newState->currentChat) {
          qDebug() << "Total mensajes en chat:" << newState->currentChat->count;
          messageModel->setChatHistory(newState->currentChat);
        }
      });

  QObject::connect(
      chatUsers, &QListView::clicked, [&](const QModelIndex &index) {
        if (index.isValid()) {
          QString username = index.data(UWUUserModel::UsernameRole).toString();
          qDebug() << "Clicked Username for messages:" << username;

          QByteArray usernameUtf8 = username.toUtf8();
          UWU_ChatHistory *history = (UWU_ChatHistory *)hashmap_get(
              &UWU_STATE->Chats, usernameUtf8.constData(), usernameUtf8.size());

          if (history) {
            messageModel->setChatHistory(history);
            UWU_STATE->currentChat = history;
          } else {
            qDebug() << "Chat history not found for user:" << username;
            messageModel->setChatHistory(nullptr);
            UWU_STATE->currentChat = nullptr;
          }

          get_messages_handler(&UWU_STATE->currentChat->channel_name);
        }
      });

  UWUUserDelegate *userDelegate = new UWUUserDelegate();
  chatUsers->setItemDelegate(userDelegate);

  QObject::connect(chatUsers, &QListView::entered, chatUsers,
                   [=](const QModelIndex &index) { chatUsers->update(index); });

  // Create button for handling busy status
  StatusButton *statusButton =
      new StatusButton(state.CurrentUser.status, controller);

  QString msg;
  // Create text input
  ChatLineEdit *chatInput = new ChatLineEdit(&msg);
  QString selectedUser;

  // Create button to send message
  ChatSendButton *sendInput =
      new ChatSendButton(&msg, chatInput, &selectedUser);

  QObject::connect(
      chatUsers->selectionModel(), &QItemSelectionModel::currentRowChanged,
      [&](const QModelIndex &current, const QModelIndex &previous) {
        if (current.isValid()) {
          selectedUser = current.data(UWUUserModel::UsernameRole).toString();
          qDebug() << "Selected user:" << selectedUser;
        } else {
          selectedUser.clear();
        }
      });

  // Generates Widgets
  QWidget *mainWidget = new QWidget();
  QWidget *topWidget = new QWidget();
  QWidget *nameWidget = new QWidget();
  QWidget *centralWidget = new QWidget();
  QWidget *chatListWidget = new QWidget();
  QWidget *chatWidget = new QWidget();
  QWidget *inputWidget = new QWidget();
  mainWindow.setCentralWidget(mainWidget);

  // Sets Layouts
  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->setContentsMargins(0, 0, 0, 0);
  QHBoxLayout *centralLayout = new QHBoxLayout();
  centralLayout->setContentsMargins(0, 0, 0, 0);
  QHBoxLayout *topLayout = new QHBoxLayout();
  topLayout->setContentsMargins(0, 0, 10, 0);
  QVBoxLayout *chatListLayout = new QVBoxLayout();
  chatListLayout->setContentsMargins(0, 0, 0, 0);
  QVBoxLayout *chatAreaLayout = new QVBoxLayout();
  chatAreaLayout->setContentsMargins(0, 0, 5, 5);
  QVBoxLayout *nameLayout = new QVBoxLayout();
  nameLayout->setContentsMargins(0, 0, 0, 0);
  QHBoxLayout *inputLayout = new QHBoxLayout();
  inputLayout->setContentsMargins(0, 0, 0, 0);

  QLabel *ipLabel = new QLabel(ip);
  ipLabel->setContentsMargins(10, 0, 0, 0);
  ipLabel->setText(QString::fromUtf8(ip));
  QLabel *nameLabel = new QLabel(username);
  nameLabel->setContentsMargins(10, 0, 0, 0);
  nameLabel->setStyleSheet("font-size: 25px;");

  QPushButton *helpButton = new QPushButton();
  helpButton->setIcon(QIcon("icons/question-icon.jpg"));
  helpButton->setMaximumWidth(50);
  helpButton->setContentsMargins(0, 0, 0, 100);

  //------------------------
  // Modal for help
  //------------------------
  QObject::connect(helpButton, &QPushButton::clicked, [&]() {
    QDialog modal(mainWidget);
    modal.setWindowTitle("Help");
    modal.setModal(true);
    QLabel label("In this chat you can talk to other people connected to it!");
    QLabel instrucctions(
        "You can:\n1. Chat with another user: Just by selecting one of the "
        "chats on the left\nyou can send messages to that person");
    QLabel instrucctions2("2. Use the general chat: This receives messages "
                          "from all users connected for everyone to see");
    QLabel instrucctions3("3. List connected users: At the left of the chat it "
                          "apperas all the people that is using the chat, \n "
                          "you can see their name and current status");
    QLabel instrucctions4(
        "4. Change status: You can change your status between ACTIVE (blue), "
        "BUSY (red), INACTIVE (yellow) and DISCONNECTED (gray)\n Your initial "
        "status is ACTIVE if there is no activy for a few seconds your status "
        "change to INACTIVE, to go back to ACTIVE you just have to send a "
        "message.\nIf you are ACTIVE or INACTIVE and you press the status "
        "button you change to BUSY status, to change from BUSY just press "
        "again the status button and you go back to ACTIVE.\n To be "
        "DISCONNECTED just close your chat session");

    QPushButton closeButton("Close");
    QVBoxLayout modalLayout(&modal);
    modalLayout.addWidget(&label);
    modalLayout.addWidget(&instrucctions);
    modalLayout.addWidget(&instrucctions2);
    modalLayout.addWidget(&instrucctions3);
    modalLayout.addWidget(&instrucctions4);
    modalLayout.addWidget(&closeButton);

    QObject::connect(&closeButton, &QPushButton::clicked, &modal,
                     &QDialog::accept);
    modal.exec();
  });

  QPalette topPalette = nameLabel->palette();
  topPalette.setColor(QPalette::Window, QColor(31, 181, 25));
  topWidget->setAutoFillBackground(true);
  topWidget->setPalette(topPalette);

  nameLayout->addWidget(nameLabel);
  nameLayout->addWidget(ipLabel);
  nameWidget->setLayout(nameLayout);

  topLayout->addWidget(nameWidget);
  topLayout->addWidget(statusButton);
  topLayout->addWidget(helpButton);
  topWidget->setLayout(topLayout);

  chatListWidget->setLayout(chatListLayout);
  chatListLayout->addWidget(chatUsers);

  inputLayout->addWidget(chatInput);
  inputLayout->addWidget(sendInput);
  inputWidget->setLayout(inputLayout);

  chatAreaLayout->addWidget(chatMessages, 1);
  chatAreaLayout->addWidget(inputWidget, 0);
  chatWidget->setLayout(chatAreaLayout);

  centralLayout->addWidget(chatListWidget, 0);
  centralLayout->addWidget(chatWidget, 1);
  centralWidget->setLayout(centralLayout);

  mainLayout->addWidget(topWidget);
  mainLayout->addWidget(centralWidget, 1);
  mainWidget->setLayout(mainLayout);

  mainWindow.resize(1200, 1000);
  mainWindow.show();

  controller->setIpLabel(ipLabel);

  // TOAST for showing errors
  // The message received from the server does not figure out on the protocol
  Toast* undeclaredMessageToast = new Toast("MESSAGE NOT FOUND IN THIS PROTOCOL!", &mainWindow);
  QObject::connect(controller, &Controller::gotInvalidMessage, undeclaredMessageToast, &Toast::showToast);

  // The user you tried to access doesn't exist!
  Toast* invalidUserToast = new Toast("THIS USER DOESN'T EXIST!", &mainWindow);
  QObject::connect(controller, &Controller::userNotFound, invalidUserToast, &Toast::showToast);

  // The status you want to change to doesn't exist!
  Toast* invalidStatusToast = new Toast("THE STATUS DOESN'T EXIST!", &mainWindow);
  QObject::connect(controller, &Controller::invalidStatus, invalidStatusToast, &Toast::showToast);

  // The message you wish to send is empty!
  Toast* invalidMessageToast = new Toast("INVALID EMPTY MESSAGE!", &mainWindow);
  QObject::connect(controller, &Controller::emptyMessage, invalidMessageToast, &Toast::showToast);

  // You're trying to communicate with a disconnected user!
  Toast* invalidCommunicationToast = new Toast("CAN'T COMMUNICATE TO DISCONNECTED USER!", &mainWindow);
  QObject::connect(controller, &Controller::userAlreadyDisconnected, invalidCommunicationToast, &Toast::showToast);
  
  int ret = app.exec();
  return ret;
}