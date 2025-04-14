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
#include <cstdio>

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

int main(int argc, char *argv[]) {
    bool isBusy = false;
    char name[] = "Jose"; 
    QApplication app(argc, argv);

    // Crear la ventana principal
    MainWindow mainWindow;

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

    QLabel *chatListLabel = new QLabel("Lista de Usuarios");
    chatListLabel->setMinimumWidth(300);
    QLabel *chatAreaLabel = new QLabel("Área de Chat");
    
    //Button for changing status to Busy or Active
    QPushButton *statusButton = new QPushButton();
    statusButton->setMaximumWidth(50);
    statusButton->setContentsMargins(0, 0, 0, 100);

    QPalette topPalette = nameLabel->palette();
    topPalette.setColor(QPalette::Window, QColor(200, 220, 255)); // Color azul claro
    topWidget->setAutoFillBackground(true);
    topWidget->setPalette(topPalette);

    QPalette chatListPalette = chatListLabel->palette();
    chatListPalette.setColor(QPalette::Window, QColor(255, 230, 200)); // Color naranja claro
    chatListWidget->setAutoFillBackground(true);
    chatListWidget->setPalette(chatListPalette);

    QPalette chatAreaPalette = chatAreaLabel->palette();
    chatAreaPalette.setColor(QPalette::Window, QColor(220, 255, 220)); // Color verde claro
    chatWidget->setAutoFillBackground(true);
    chatWidget->setPalette(chatAreaPalette);

    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(ipLabel);
    nameWidget->setLayout(nameLayout);

    topLayout->addWidget(nameWidget);
    topLayout->addWidget(activeButton);
    topWidget->setLayout(topLayout);
    
    chatListLayout->addWidget(chatListLabel);
    chatListWidget->setLayout(chatListLayout);
    
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

