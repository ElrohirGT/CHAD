#include "../lib/lib.c"
#include "deps/hashmap/hashmap.h"
#include "mongoose.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QModelIndex>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPainter>
#include <QPushButton>
#include <QRunnable>
#include <QString>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// ***********************************************
// CONSTANTS
// ***********************************************
static const size_t MAX_MESSAGES_PER_CHAT = 100;
static const size_t MAX_CHARACTERS_INPUT = 254;

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

  UWU_UserListNode node = UWU_UserListNode_newWithValue(group_user);
  UWU_UserList_insertEnd(&state.ActiveUsers, &node, err);
  if (err != NO_ERROR) {
    UWU_PANIC("Fatal: Failed to insert Group chat to the UserCollection!\n");
    return state;
  }

  UWU_ChatHistory *ht = (UWU_ChatHistory *)malloc(sizeof(UWU_ChatHistory));
  if (ht == NULL) {
    UWU_PANIC("Fatal: Failed to allocate memory for groupal chat history\n");
    return state;
  }
  *ht = UWU_ChatHistory_init(MAX_MESSAGES_PER_CHAT, group_name, err);

  if (0 != hashmap_put(&state.Chats, group_name.data, group_name.length, ht)) {
    UWU_PANIC("Fatal: Error creating a chat entry for the group chat.");
  }

  // SET CURRENT CHAT
  state.currentChat = NULL;

  UWU_String_freeWithMalloc(&group_name);

  return state;
}

void deinit_ClientState(UWU_ClientState *state) {
  MG_INFO(("Cleaning current chat pointer..."));
  state->currentChat = NULL;

  MG_INFO(("Cleaning Current User"));
  UWU_String_freeWithMalloc(&state->CurrentUser.username);

  MG_INFO(("Cleaning Client IP"));

  MG_INFO(("Cleaning User List..."));
  UWU_UserList_deinit(&state->ActiveUsers);

  MG_INFO(("Cleaning DM Chat histories..."));
  hashmap_destroy(&state->Chats);

  MG_INFO(("Cleaning connection url..."));
  free(s_url);
}

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
void store_ip_addr(struct mg_addr *addr) {
  if (addr->is_ip6) {
    // IPv6
    const char *result = inet_ntop(AF_INET6, addr->ip, ip, sizeof(ip));
    if (result == NULL) {
      UWU_PANIC("Unable to store ip addr v6");
    }
  } else {
    // IPv4
    const char *result = inet_ntop(AF_INET, addr->ip, ip, sizeof(ip));
    if (result == NULL) {
      UWU_PANIC("Unable to store ip addr v6");
    }
  }

  printf("Stored IP: %s\n", ip);
  printf("Port: %u\n", ntohs(addr->port));
}

// *******************************************
// CONTROLLER
// *******************************************

// Object responsable for handling a message from the WS.
// One is created per message from tehe server.
class MessageTask : public QObject, public QRunnable {
  Q_OBJECT
public:
  MessageTask(UWU_String s) : msg(s) {}

  // Logic to handle message processing.
  void run() override {
    printf("HANDLING MESSAGE...\n");
    printf("GOT ECHO REPLY: [%.*s]\n", (int)msg.length, msg.data);

    QMutexLocker locker(&mutex);
    switch (msg.data[0]) {
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
      fprintf(stderr, "Error: Unrecognized message from server!\n");
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

public slots:

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
    } else if (ev == MG_EV_CONNECT && mg_url_is_ssl(s_url)) {
      // On connection established
      struct mg_tls_opts opts = {.name = mg_url_host(s_url)};
      mg_tls_init(c, &opts);
    } else if (ev == MG_EV_ERROR) {
      // On error, log error message
      MG_ERROR(("%p %s", c->fd, (char *)ev_data));
    } else if (ev == MG_EV_WS_OPEN) {
      // When websocket handshake is successful, send message
      store_ip_addr(&c->loc);
    } else if (ev == MG_EV_WS_MSG) {

      // Copying message
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
      UWU_Err err = NO_ERROR;
      UWU_String msg = UWU_String_fromMongoose(&wm->data, err);

      // Launching thread
      MessageTask *task = new MessageTask(msg);
      QObject::connect(task, &MessageTask::msgPrococessed, controller,
                       &Controller::onMsgProcessed, Qt::QueuedConnection);
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
    emit stateChanged();
  }

signals:
  // Signal to announce UWU_STATE has changed.
  void stateChanged();
  void finished();
};

// HANDLERS
// ****************************

// Fetch list of users
void list_users_handler() {
  char data[1] = {1};
  mg_ws_send(ws_conn, data, 1, WEBSOCKET_OP_BINARY);
  printf("LIST USERS!");
}

// NOTE: MIGHT NOT BE USEFUL FOR OUR PURPOSES
void get_user_handler() {
  if (UWU_STATE == NULL) {
    return;
  }
}

// Change status of the current user
void tooggle_status_handler() {
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

  if (currentUser->status == ACTIVE || currentUser->status == INACTIVE) {
    data[length - 1] = BUSY;
  } else {
    data[length - 1] = ACTIVE;
  }

  mg_ws_send(ws_conn, data, length, WEBSOCKET_OP_BINARY);
  free(data);
}

// Fetch messages for the current chat history selected
void get_messages_handler() {}

// Sends a chat message with given history
void send_message_handler(UWU_String *text) {
  // TODO: handle when no chat is selected
  UWU_ChatHistory *currentChat = UWU_STATE->currentChat;
  if (currentChat == NULL) {
    return;
  }
  size_t username_length = currentChat->channel_name.length;
  size_t message_length = text->length;
  size_t length = 4 + username_length + message_length;
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
    data[2 + i] = UWU_String_charAt(&currentChat->channel_name, i);
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
class ActiveButton : public QPushButton {
  Q_OBJECT
public:
  ActiveButton(bool &RefStatus, QWidget *parent = nullptr)
      : QPushButton(parent), status(false), externalStatus(RefStatus) {
    setMaximumWidth(50);
    setContentsMargins(0, 0, 0, 100);

    connect(this, &QPushButton::clicked, this, &ActiveButton::clickSlot);
  }

public slots:
  // Change to opposite status set status true for Busy
  void clickSlot() {
    status = !status;                // change class status
    qDebug() << "Status:" << status; // Debug print
    externalStatus = status;         // change the status from outside the class
    qDebug() << "main status:" << externalStatus; // Debug print
    update();
  }

protected:
  void paintEvent(QPaintEvent *event) override {
    QPushButton::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor circleColor = status ? QColor(0, 255, 0) : QColor(255, 0, 0);
    painter.setBrush(circleColor);
    painter.setPen(Qt::NoPen);

    int radius = 10;
    int centerX = width() / 2;
    int centerY = height() / 2;

    painter.drawEllipse(QPoint(centerX, centerY), radius, radius);
  }

  // Class attributes
private:
  bool status;
  bool &externalStatus;
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

private:
  char username_[255];
  char ip_[255];
};

struct UserData {
  QString name;
  QString ip;
};

class UWUUserModel : public QAbstractListModel {
  Q_OBJECT
public:
  UWUUserModel(const std::vector<UserData> &users, QObject *parent = nullptr)
      : QAbstractListModel(parent), userDataList(users) {}

  int rowCount(const QModelIndex &parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : userDataList.size();
  }

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= userDataList.size()) {
      return QVariant();
    }

    const UserData &user = userDataList[index.row()];

    if (role == Qt::DisplayRole) {
      // Puedes retornar algo simple para el texto por defecto si es necesario
      return user.name;
    } else if (role == Qt::UserRole + 1) {
      // Retornar los datos del usuario para el delegado
      return QVariant::fromValue(user);
    }
    return QVariant();
  }

private:
  std::vector<UserData> userDataList;
};

class UWUUserDelegate : public QStyledItemDelegate {
  Q_OBJECT
public:
  UWUUserDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    if (!index.isValid())
      return;

    QVariant userDataVar = index.data(Qt::UserRole + 1);
    if (!userDataVar.canConvert<UserData>()) {
      QStyledItemDelegate::paint(painter, option, index);
      return;
    }

    UserData user = userDataVar.value<UserData>();

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

    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, user.name);
    painter->setFont(option.font); // IP sin negrita
    painter->drawText(ipRect, Qt::AlignLeft | Qt::AlignVCenter, user.ip);

    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override {
    UWUUserQT tempWidget("dummy", "dummy");
    return QSize(option.rect.width(), tempWidget.sizeHint().height());
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
  QObject::connect(thread, &QThread::finished, thread, &QThread::deleteLater);
  QObject::connect(&app, &QCoreApplication::aboutToQuit,
                   [controller]() { controller->stop(); });

  // Start the thread
  thread->start();

  // LAYOUT INITIALIZATION
  // **********************

  std::vector<UserData> users = {
      {"Jose", "1.2.3"}, {"Maria", "4.5.6"}, {"Pedro", "7.8.9"},
      {"Jose", "1.2.3"}, {"Maria", "4.5.6"}, {"Pedro", "7.8.9"},
      {"Jose", "1.2.3"}, {"Maria", "4.5.6"}, {"Pedro", "7.8.9"},
      {"Jose", "1.2.3"}, {"Maria", "4.5.6"}, {"Pedro", "7.8.9"},
      {"Jose", "1.2.3"}, {"Maria", "4.5.6"}, {"Pedro", "7.8.9"},
      {"Jose", "1.2.3"}, {"Maria", "4.5.6"}, {"Pedro", "7.8.9"},
  };

  bool isBusy = false;
  char name[] = "Jose";

  // Crear la ventana principal
  MainWindow mainWindow;

  UWUUserModel *userModel = new UWUUserModel(users);
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

  UWUUserDelegate *userDelegate = new UWUUserDelegate();
  chatUsers->setItemDelegate(userDelegate);

  QObject::connect(chatUsers, &QListView::entered, chatUsers,
                   [=](const QModelIndex &index) { chatUsers->update(index); });

  // Create button for handling busy status
  ActiveButton *activeButton = new ActiveButton(isBusy);

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

  QLabel *ipLabel = new QLabel("127.0.0.1");
  ipLabel->setContentsMargins(10, 0, 0, 0);
  QLabel *nameLabel = new QLabel(name);
  nameLabel->setContentsMargins(10, 0, 0, 0);
  nameLabel->setStyleSheet("font-size: 25px;");

  QListView *chatMessages = new QListView();

  QPushButton *helpButton = new QPushButton();
  helpButton->setIcon(QIcon("icons/question-icon.jpg"));
  helpButton->setMaximumWidth(50);
  helpButton->setContentsMargins(0, 0, 0, 100);
  QPushButton *statusButton = new QPushButton();
  statusButton->setMaximumWidth(50);
  statusButton->setContentsMargins(0, 0, 0, 100);

  QPalette topPalette = nameLabel->palette();
  topPalette.setColor(QPalette::Window, QColor(31, 181, 25));
  topWidget->setAutoFillBackground(true);
  topWidget->setPalette(topPalette);

  nameLayout->addWidget(nameLabel);
  nameLayout->addWidget(ipLabel);
  nameWidget->setLayout(nameLayout);

  topLayout->addWidget(nameWidget);
  topLayout->addWidget(activeButton);
  topLayout->addWidget(helpButton);
  topWidget->setLayout(topLayout);

  chatListWidget->setLayout(chatListLayout);
  chatListLayout->addWidget(chatUsers);

  QLineEdit *chatInput = new QLineEdit();
  chatInput->setPlaceholderText("Write a message");

  QPushButton *sendInput = new QPushButton();
  sendInput->setMaximumWidth(100);
  sendInput->setIcon(QIcon("icons/send-icond.png"));

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

  int ret = app.exec();
  return ret;
}
