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
    mainWindow.setWindowTitle("Ejemplo de UI Básica con Editor de Texto en Qt");

    // 2. Crear el widget central
    QWidget *centralWidget = new QWidget();
    mainWindow.setCentralWidget(centralWidget);

    // 3. Crear los widgets de la interfaz
    QTextEdit *textEditor = new QTextEdit();
    QPushButton *getTextButton = new QPushButton("Obtener Texto");
    QLabel *textLengthLabel = new QLabel("Longitud del texto: 0");

    // 4. Crear layouts
    QVBoxLayout *mainLayout = new QVBoxLayout();
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    // Añadir el editor de texto al layout principal
    mainLayout->addWidget(textEditor);

    // Añadir el botón y la etiqueta al layout horizontal
    buttonLayout->addWidget(getTextButton);
    buttonLayout->addWidget(textLengthLabel);

    // Añadir el layout horizontal al layout principal
    mainLayout->addLayout(buttonLayout);

    // Establecer el layout principal como el layout del widget central
    centralWidget->setLayout(mainLayout);

    // 5. Crear conexiones (signals/slots)
    QObject::connect(getTextButton, &QPushButton::clicked, [&]() {
        QString text = textEditor->toPlainText();
        int length = text.length();
        textLengthLabel->setText("Longitud del texto: " + QString::number(length));
    });

    // 6. Mostrar la ventana principal
    mainWindow.show();

    // 7. Iniciar el bucle de eventos
    return app.exec();
}
