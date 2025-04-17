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
#include <QObject>
#include <QPainter>
#include <QPushButton>
#include <QString>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <cstdio>
#include <cstring>
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
  UWU_User UWU_CurrentUser;

  // Stores the users the client can messages to.
  UWU_UserList UWU_ActiveUsers;
  // A pointer to the current selected chat, set to NULL if no client is
  // selected.
  UWU_ChatHistory *UWU_currentChat;
  // Group chat user
  UWU_User UWU_GroupChat;
  // Saves all the chat histories.
  // Key: The name of the user this client chat chat to.
  // Value: An UWU_History item.
  struct hashmap_s chats;
};

UWU_ClientState *UWU_STATE = NULL;

// Connection to the websocket server
static mg_connection *ws_conn;

static const char *s_url = "ws://ws.vi-server.org/mirror/";
static const char *s_ca_path = "ca.pem";

void init_ClientState(UWU_Err *err) {}

void deinit_ClientState(UWU_Err *err) {}

// *******************************************
// CONTROLLER
// *******************************************

// Worker function that handles a message
static void handle_msg(struct mg_connection *c, int ev, void *ev_data);

class Controller : public QObject {
  Q_OBJECT
  struct mg_mgr mgr;
  bool done = false;

public slots:

  // Callback function called by moongose on event
  static void ws_listener(struct mg_connection *c, int ev, void *ev_data) {}

  // Entrypoint to start the controller client
  void run() {
    qDebug() << "Worker is running in thread:" << QThread::currentThread();

    mg_mgr_init(&mgr);       // Initialise event manager
    mg_log_set(MG_LL_DEBUG); // Set log level
    ws_conn =
        mg_ws_connect(&mgr, s_url, ws_listener, &done, NULL); // Create client

    // initializing client
    QTimer *pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, [=]() {
      mg_mgr_poll(&mgr, 100); // Does same work, but async-friendly
    });
    pollTimer->start(50);
  }
  // Cleaning function to call when window is closed
  void stop() {
    mg_mgr_free(&mgr);
    printf("Cleaning resources...");
  }

signals:
  void finished();
};

static void ws_listener(struct mg_connection *c, int ev, void *ev_data) {

};

// Holds all logic related to receiving messages from the server.
// Print websocket response and signal that we're done
static void handle_msg(struct mg_connection *c, int ev, void *ev_data) {
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
    printf("Hello World\n");
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
  QApplication app(argc, argv);

  // **********************
  // CONTROLLER AND WS CLIENT CONNECTION INITALIZAITON
  // **********************

  QThread *thread = new QThread;
  Controller *controller = new Controller;
  controller->moveToThread(thread);

  QObject::connect(thread, &QThread::started, controller, &Controller::run);
  QObject::connect(controller, &Controller::finished, thread, &QThread::quit);
  QObject::connect(controller, &Controller::finished, controller,
                   &Controller::deleteLater);
  QObject::connect(thread, &QThread::finished, controller,
                   &QThread::deleteLater);
  QObject::connect(&app, &QCoreApplication::aboutToQuit, controller,
                   &Controller::stop);

  // Start the thread
  thread->start();

  // **********************
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

  return app.exec();
}
