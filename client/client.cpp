#include <QApplication>
#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QString>
#include <QCloseEvent>
#include <QPushButton>
#include <QDebug>
#include <QListView>
#include <QStringListModel>
#include <QPainter>
#include <QVariant>
#include <QObject>
#include <cstdio>
#include <cstring>
#include <vector>
#include <QStyledItemDelegate>
#include <QModelIndex>
#include <QAbstractItemView>


// class for creating the window project
class MainWindow : public QMainWindow {
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
    public: 
        ActiveButton(bool &RefStatus, QWidget *parent = nullptr) : QPushButton(parent), status(false), externalStatus(RefStatus) {
            setMaximumWidth(50);
            setContentsMargins(0,0,0,100);
            
            connect(this, &QPushButton::clicked, this, &ActiveButton::clickSlot);
        }

        public slots:
        // Change to opposite status set status true for Busy
        void clickSlot(){
            status = !status; // change class status
            qDebug() << "Status:" << status; // Debug print
            externalStatus = status; // change the status from outside the class
            qDebug() << "main status:" << externalStatus; // Debug print
        }
    
        // Class attributes
        private:
        bool status;
        bool &externalStatus;
};

class UWUUserQT : public QWidget {

    public:
        UWUUserQT(const char username[], const char ip[], QWidget *parent = nullptr) : QWidget(parent) {
    
        std::strncpy(username_, username, sizeof(username_) - 1);
        username_[sizeof(username_) - 1] = '\0';
    
        std::strncpy(ip_, ip, sizeof(ip_) - 1);
        ip_[sizeof(ip_) - 1] = '\0';
        QVBoxLayout *verticalLayout = new QVBoxLayout(this);
        QLabel *nameLabel = new QLabel(username);
        QLabel *ipLabel = new QLabel(ip);
        
        verticalLayout->addWidget(nameLabel);
        verticalLayout->addWidget(ipLabel);
    
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
public:
    UWUUserModel(const std::vector<UserData>& users, QObject *parent = nullptr)
        : QAbstractListModel(parent), userDataList(users) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : userDataList.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= userDataList.size()) {
            return QVariant();
        }

        const UserData& user = userDataList[index.row()];

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
    public:
        UWUUserDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
            if (index.isValid()) {
                QVariant userDataVar = index.data(Qt::UserRole + 1);
                if (userDataVar.canConvert<UserData>()) {
                    UserData userData = userDataVar.value<UserData>();
                    UWUUserQT userWidget(userData.name.toLatin1(), userData.ip.toLatin1());
    
                    painter->save();
                    painter->translate(option.rect.topLeft());
                    userWidget.render(painter);
                    painter->restore();
                } else {
                    QStyledItemDelegate::paint(painter, option, index); // Fallback
                }
            }
        }
    
        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
            UWUUserQT tempWidget("dummy", "dummy");
            return tempWidget.sizeHint();
        }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::vector<UserData> users = {
        {"Jose", "1.2.3"},
        {"Maria", "4.5.6"},
        {"Pedro", "7.8.9"}
    };
    
    
    bool isBusy = false;
    char name[] = "Jose"; 
    
    // Crear la ventana principal
    MainWindow mainWindow;
    
    UWUUserModel *userModel = new UWUUserModel(users);
    QListView *chatUsers = new QListView();
    chatUsers->setModel(userModel);

    UWUUserDelegate *userDelegate = new UWUUserDelegate();
    chatUsers->setItemDelegate(userDelegate);
    
    // Create button for handling busy status
    ActiveButton *activeButton = new ActiveButton(isBusy);
    
    // Generates Widgets
    // QWidgets are the base component for the QT aplicattion
    // Contains all widgets
    QWidget *mainWidget = new QWidget();
    // Contains the components of the top bar of chat
    QWidget *topWidget = new QWidget();
    // Contains name and ip address
    QWidget *nameWidget = new QWidget();
    // Contains all widgets remaining 
    QWidget *centralWidget = new QWidget();
    // Widget that shows the list of users to chat with
    QWidget *chatListWidget = new QWidget();
    // Widget where user sees and send messages
    QWidget *chatWidget = new QWidget();
    mainWindow.setCentralWidget(mainWidget);

    // Sets Layouts
    // Layouts organizes geometrically the elements inside a Widget
    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout *centralLayout = new QHBoxLayout();
    centralLayout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *chatListLayout = new QVBoxLayout();
    chatListLayout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *chatAreaLayout = new QVBoxLayout();
    chatAreaLayout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *nameLayout = new QVBoxLayout();
    nameLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *ipLabel = new QLabel("127.0.0.1");
    ipLabel->setContentsMargins(10,0,0,0);
    QLabel *nameLabel = new QLabel(name);
    nameLabel->setContentsMargins(10,0,0,0);
    nameLabel->setStyleSheet("font-size: 25px;");

    QLabel *chatAreaLabel = new QLabel("Área de Chat");
    
    //Button for changing status to Busy or Active
    QPushButton *statusButton = new QPushButton();
    statusButton->setMaximumWidth(50);
    statusButton->setContentsMargins(0, 0, 0, 100);

    QPalette topPalette = nameLabel->palette();
    topPalette.setColor(QPalette::Window, QColor(31, 181, 25));
    topWidget->setAutoFillBackground(true);
    topWidget->setPalette(topPalette);

    QPalette chatAreaPalette = chatAreaLabel->palette();
    chatAreaPalette.setColor(QPalette::Window, QColor(189, 189, 189));
    chatWidget->setAutoFillBackground(true);
    chatWidget->setPalette(chatAreaPalette);

    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(ipLabel);
    nameWidget->setLayout(nameLayout);

    topLayout->addWidget(nameWidget);
    topLayout->addWidget(activeButton);
    topWidget->setLayout(topLayout);
    
    chatListWidget->setLayout(chatListLayout);
    chatListLayout->addWidget(chatUsers);
    
    chatAreaLayout->addWidget(chatAreaLabel);
    chatWidget->setLayout(chatAreaLayout);
    
    centralLayout->addWidget(chatListWidget);
    centralLayout->addWidget(chatWidget, 1);
    centralWidget->setLayout(centralLayout);

    mainLayout->addWidget(topWidget);
    mainLayout->addWidget(centralWidget, 1);
    mainWidget->setLayout(mainLayout);

    // 4. Crear layouts

    // Añadir el editor de texto al layout principal
    //mainLayout->addWidget(textEditor);

    // Añadir el botón y la etiqueta al layout horizontal
    //buttonLayout->addWidget(getTextButton);
    //buttonLayout->addWidget(textLengthLabel);

    // Añadir el layout horizontal al layout principal

    // Establecer el layout principal como el layout del widget central

    // 6. Mostrar la ventana principal
    mainWindow.resize(1000, 1500);
    mainWindow.show();

    // 7. Iniciar el bucle de eventos
    return app.exec();
}

