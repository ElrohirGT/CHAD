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
            
        QPalette UWUUSerQTPallete = this->palette();
        UWUUSerQTPallete.setColor(QPalette::Window, QColor(189, 189, 189));
        this->setAutoFillBackground(true);
        this->setPalette(UWUUSerQTPallete);

        QVBoxLayout *verticalLayout = new QVBoxLayout(this);
        verticalLayout->setContentsMargins(0,0,0,0);
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
        
                    userWidget.setFixedSize(option.rect.size());
        
                    painter->save();
                    painter->translate(option.rect.topLeft());
                    userWidget.render(painter);
                    painter->restore();

                    painter->save();
                    QPen pen(Qt::black);
                    pen.setWidth(1);
                    painter->setPen(pen);
                    painter->drawRect(option.rect.adjusted(0, 0, -1, -1));
                    painter->restore();
                } else {
                    QStyledItemDelegate::paint(painter, option, index); // Fallback
                }
            }
        }
        
    
        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
            UWUUserQT tempWidget("dummy", "dummy");
            return QSize(option.rect.width(), tempWidget.sizeHint().height());
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
    chatUsers->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // Añadido

    UWUUserDelegate *userDelegate = new UWUUserDelegate();
    chatUsers->setItemDelegate(userDelegate);

    // Create button for handling busy status
    ActiveButton *activeButton = new ActiveButton(isBusy);

    // Generates Widgets
    QWidget *mainWidget = new QWidget();
    QWidget *topWidget = new QWidget();
    QWidget *nameWidget = new QWidget();
    QWidget *centralWidget = new QWidget();
    QWidget *chatListWidget = new QWidget();
    QWidget *chatWidget = new QWidget();
    mainWindow.setCentralWidget(mainWidget);

    // Sets Layouts
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
    chatListLayout->addWidget(chatAreaLabel);

    chatAreaLayout->addWidget(chatAreaLabel);
    chatWidget->setLayout(chatAreaLayout);

    centralLayout->addWidget(chatListWidget, 0); // Ajustado stretch
    centralLayout->addWidget(chatWidget, 1);
    centralWidget->setLayout(centralLayout);

    mainLayout->addWidget(topWidget);
    mainLayout->addWidget(centralWidget, 1);
    mainWidget->setLayout(mainLayout);

    mainWindow.resize(1000, 1500);
    mainWindow.show();

    return app.exec();
}