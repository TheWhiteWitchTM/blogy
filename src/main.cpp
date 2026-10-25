#include "config.h"
#include "selftest.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QIcon>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTimer>

namespace {

void applyTheme(QApplication &app)
{
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app.setStyle(fusion);
    QFont ui(QStringLiteral("Fira Sans"), 11);
    if (!QFontInfo(ui).exactMatch())
        ui = QFont(QStringLiteral("DejaVu Sans"), 11);
    app.setFont(ui);

    QPalette pal;
    const QColor black(QStringLiteral("#000000"));
    const QColor text(QStringLiteral("#e8e6e3"));
    const QColor ember(QStringLiteral("#ffb454"));
    pal.setColor(QPalette::Window, black);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, black);
    pal.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#0e0e0e")));
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, QColor(QStringLiteral("#141414")));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::Highlight, ember);
    pal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#1a1206")));
    pal.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#141414")));
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#6a6a6a")));
    pal.setColor(QPalette::BrightText, ember);
    pal.setColor(QPalette::Link, ember);
    app.setPalette(pal);

    QFile file(QStringLiteral(":/style.qss"));
    if (file.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(file.readAll()));
    QIcon icon = QIcon::fromTheme(QStringLiteral("blogy"));
    if (icon.isNull())
        icon = QIcon(QStringLiteral(":/icon-512.png"));
    icon.addFile(QStringLiteral(":/icon.svg"));
    app.setWindowIcon(icon);
}

} // namespace

int main(int argc, char *argv[])
{
    bool selfTest = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--self-test") == 0)
            selfTest = true;
    }
    if (selfTest)
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("blogy"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("blogy.local"));
    QCoreApplication::setApplicationName(QStringLiteral("blogy"));
    QApplication::setApplicationDisplayName(QStringLiteral("Blogy"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("blogy"));
    applyTheme(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A black Markdown editor for GitHub Pages blogs."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("self-test")},
                                        QStringLiteral("Run the built-in checks and exit.")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("config")},
                                        QStringLiteral("Use this config file instead of ~/.config/blogy/config.json."),
                                        QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("quit-after")},
                                        QStringLiteral("Close the window after this many milliseconds (smoke tests)."),
                                        QStringLiteral("ms")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("screenshot")},
                                        QStringLiteral("Save a PNG of the window after start-up and exit (smoke tests)."),
                                        QStringLiteral("png")));
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Posts to open."));
    parser.process(app);
    if (parser.isSet(QStringLiteral("self-test")))
        return runSelfTest();

    Config config;
    QString error;
    const QString path = parser.value(QStringLiteral("config"));
    if (!config.load(path, &error))
        QMessageBox::warning(nullptr, QStringLiteral("Blogy"),
                             QStringLiteral("Could not read %1:\n%2\n\nUsing the example sites for now.").arg(config.path(), error));
    else
        config.ensureExists(&error); // first run: write the example config with empty secrets

    MainWindow window(&config);
    window.openPaths(parser.positionalArguments());
    window.show();
    if (parser.isSet(QStringLiteral("quit-after")))
        QTimer::singleShot(parser.value(QStringLiteral("quit-after")).toInt(), &app, [] { QCoreApplication::exit(0); });
    if (parser.isSet(QStringLiteral("screenshot"))) {
        const QString png = parser.value(QStringLiteral("screenshot"));
        QTimer::singleShot(1500, &window, [&window, png] {
            window.grab().save(png);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}
