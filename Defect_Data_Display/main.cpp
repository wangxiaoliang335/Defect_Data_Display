#include "Defect_Data_Display.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Defect_Data_Display window;
    window.show();
    window.resize(1920, 1080);
    return app.exec();
}
