#include <string.h>
#include <iostream>
#include <fstream>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QObject>

#include "cc3200.h"
#include "cli.h"
#include "dialog.h"
#include "esp8266.h"
#include "flasher.h"
#include "sigsource.h"

namespace {

using std::cout;
using std::cerr;
using std::endl;

static int verbosity = 0;
static std::ostream* logfile = &cerr;

void outputHandler(QtMsgType type, const QMessageLogContext& context,
                   const QString& msg) {
  QByteArray localMsg = msg.toLocal8Bit();
  switch (type) {
    case QtDebugMsg:
      if (verbosity >= 4) {
        *logfile << "DEBUG: ";
        if (context.file != NULL) {
          *logfile << context.file << ":" << context.line;
        }
        if (context.function != NULL) {
          *logfile << " (" << context.function << "): ";
        }
        *logfile << localMsg.constData() << endl;
      }
      break;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 5, 0))
    case QtInfoMsg:
      if (verbosity >= 3) {
        *logfile << "INFO: ";
        if (context.file != NULL) {
          *logfile << context.file << ":" << context.line;
        }
        if (context.function != NULL) {
          *logfile << " (" << context.function << "): ";
        }
        *logfile << localMsg.constData() << endl;
      }
      break;
#endif
    case QtWarningMsg:
      if (verbosity >= 2) {
        *logfile << "WARNING: ";
        if (context.file != NULL) {
          *logfile << context.file << ":" << context.line;
        }
        if (context.function != NULL) {
          *logfile << " (" << context.function << "): ";
        }
        *logfile << localMsg.constData() << endl;
      }
      break;
    case QtCriticalMsg:
      if (verbosity >= 1) {
        *logfile << "CRITICAL: ";
        if (context.file != NULL) {
          *logfile << context.file << ":" << context.line;
        }
        if (context.function != NULL) {
          *logfile << " (" << context.function << "): ";
        }
        *logfile << localMsg.constData() << endl;
      }
      break;
    case QtFatalMsg:
      *logfile << "FATAL: ";
      if (context.file != NULL) {
        *logfile << context.file << ":" << context.line;
      }
      if (context.function != NULL) {
        *logfile << " (" << context.function << "): ";
      }
      *logfile << localMsg.constData() << endl;
      abort();
  }
}
}

int main(int argc, char* argv[]) {
  QCoreApplication::setOrganizationName("Cesanta");
  QCoreApplication::setOrganizationDomain("cesanta.com");
  QCoreApplication::setApplicationName(APP_NAME);
  QCoreApplication::setApplicationVersion(VERSION);

  QCommandLineParser parser;
  parser.setApplicationDescription("Smart.js flashing tool");
  parser.addHelpOption();
  parser.addVersionOption();

  parser.addOptions(
      {{"gui", "Run in GUI mode."},
       {{"p", "platform"},
        "Target device platform. Required. Valid values: esp8266.",
        "platform"},
       {{"l", "probe-ports"},
        "Print the list of available serial ports and try detect device "
        "presence on each of them."},
       {{"d", "debug"}, "Enable debug output. Equivalent to --V=3"},
       {"V",
        "Verbosity level. 0 – normal output, 1 - also print critical (but not "
        "fatal) errors, 2 - also print warnings, 3 - print info messages, 4 - "
        "print debug output.",
        "level", "1"},
       {"log", "Redirect logging into a file.", "filename"},
       {"port", "Serial port to use.", "port"},
       {"console-baud-rate", "Baud rate to use with the console serial port.",
        "console-baud-rate"},
       {"flash-baud-rate",
        "Baud rate to use with the serial port used for flashing.",
        "baud-rate"},
       {"probe", "Check device presence on a given port."},
       {"flash", "Flash firmware from the given directory.", "dir"},
       {Flasher::kIdDomainOption,
        "Domain name to use for generated device IDs. Default: api.cesanta.com",
        "name", "api.cesanta.com"},
       {Flasher::kOverwriteFSOption,
        "If set, force overwrite the data flash with the factory image"},
       {Flasher::kSkipIdGenerationOption,
        "If set, device ID won't be generated and flashed."},
       {"generate-id",
        "Generate a file with device ID in a format suitable for flashing.",
        "filename"},
       {"console-log",
        "If set, bytes read from a serial port in console mode will be "
        "appended to the given file.",
        "file"}});

  ESP8266::addOptions(&parser);
  CC3200::addOptions(&parser);

#ifdef Q_OS_MAC
  // Finder adds "-psn_*" argument whenever it shows the Gatekeeper prompt.
  // We can't just add it to the list of options since numbers in it are not
  // stable, so we just won't let QCommandLineParser know about that argument.
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "-psn_", 5) == 0) {
      for (int j = i + 1; j < argc; j++) {
        argv[j - 1] = argv[j];
      }
      argc--;
    }
  }
#endif

  QStringList commandline;
  for (int i = 0; i < argc; i++) {
    commandline << QString(argv[i]);
  }
  // We ignore the return value here, since there might be some options handled
  // by QApplication class. For now the most important thing we need to check
  // for is presence of "--gui" option. Later, once we have an application
  // object, we invoke parser.process(), which does the parsing again, handles
  // --help/--version and exits with error if there are still some unknown
  // options.
  parser.parse(commandline);

  if (parser.isSet("log")) {
    logfile = new std::ofstream(parser.value("log").toStdString(),
                                std::ios_base::app);
    if (logfile->fail()) {
      cerr << "Failed to open log file." << endl;
      return 1;
    }
    *logfile << "\n---------- Log started on "
             << QDateTime::currentDateTime().toString(Qt::ISODate).toStdString()
             << endl;
  }
  qInstallMessageHandler(outputHandler);
  if (parser.isSet("debug")) {
    verbosity = 4;
  } else if (parser.isSet("V")) {
    bool ok;
    verbosity = parser.value("V").toInt(&ok, 10);
    if (!ok) {
      cerr << parser.value("V").toStdString() << " is not a number" << endl
           << endl;
      return 1;
    }
  }

  if (argc == 1 || parser.isSet("gui")) {
    // Run in GUI mode.
    QApplication app(argc, argv);
    parser.process(app);
    app.setApplicationDisplayName("Smart.js flashing tool");
    MainDialog w(&parser);
    w.show();
    SigSource* ss = initSignalSource(&w);
    QObject::connect(ss, &SigSource::flash, &w, &MainDialog::loadFirmware);
    QObject::connect(ss, &SigSource::connectDisconnect, &w,
                     &MainDialog::connectDisconnectTerminal);
    return app.exec();
  }

  // Run in CLI mode.
  QCoreApplication app(argc, argv);
  parser.process(app);
  CLI cli(&parser);

  return app.exec();
}
