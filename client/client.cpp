#include <QApplication>
#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QString>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // 1. Crear la ventana principal
    QMainWindow mainWindow;
    mainWindow.setWindowTitle("CHAD GUI");

    // Generates Widgets
    // QWidgets are the base component for the QT aplicattion
    // Contains all widgets
    QWidget *mainWidget = new QWidget();
    // Contains the components of the top bar of chat
    QWidget *topWidget = new QWidget();
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
    QHBoxLayout *centralLayout = new QHBoxLayout();
    QHBoxLayout *topLayout = new QHBoxLayout();
    QVBoxLayout *chatListLayout = new QVBoxLayout();
    QVBoxLayout *chatAreaLayout = new QVBoxLayout();

    QLabel *topLabel = new QLabel("Barra Superior");
    topLabel->setMinimumHeight(30);

    QLabel *chatListLabel = new QLabel("Lista de Usuarios");
    chatListLabel->setMinimumWidth(100);
    QLabel *chatAreaLabel = new QLabel("Área de Chat");

    topLayout->addWidget(topLabel);
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
    mainWindow.showFullScreen();
    mainWindow.show();

    // 7. Iniciar el bucle de eventos
    return app.exec();
}
