#include "mainwindowshslogistics.h"

#include <QApplication>
#include <QIcon>
#include <QSplashScreen>
#include <QElapsedTimer>

static void sleepWithEventProcessing(QApplication &app, int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        app.processEvents();
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));

    QIcon appIcon(QStringLiteral(":/SHSIcon.png"));
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral("SHSIcon.png"));
    }
    a.setWindowIcon(appIcon);

    const QStringList args = a.arguments();
    const int scIdx = args.indexOf(QStringLiteral("--screenshot"));
    const bool isAutoScreenshot = (scIdx >= 0 && scIdx + 1 < args.size());

    // Start-Splashscreen im SHS Corporate Design mit Motiven aus startseite_img_de.jpg
    QSplashScreen splash(
        MainWindowSHSLogistics::createCorporateSplashPixmap(
            35, QStringLiteral("Initialisiere Google-Maps-Kartenkacheln & Werksgeometrie Völklingen ...")),
        Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);

    if (!isAutoScreenshot) {
        splash.show();
        sleepWithEventProcessing(a, 750);

        splash.setPixmap(MainWindowSHSLogistics::createCorporateSplashPixmap(
            70, QStringLiteral("Lade Quellen, Senken, Güter-Objekte & 1420 °C Torpedopfannenwagen ...")));
        sleepWithEventProcessing(a, 750);

        splash.setPixmap(MainWindowSHSLogistics::createCorporateSplashPixmap(
            100, QStringLiteral("Verbinde DB Cargo / SAVVY Telematik-Schnittstelle – Startbereit!")));
        sleepWithEventProcessing(a, 650);
    }

    MainWindowSHSLogistics w;
    w.setWindowIcon(appIcon);
    w.show();

    if (!isAutoScreenshot) {
        splash.finish(&w);
    } else {
        w.scheduleAutoScreenshots(args[scIdx + 1]);
    }

    return a.exec();
}
