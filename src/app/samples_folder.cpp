// samples_folder.cpp - Where Mapsmith's sample files are (see samples_folder.h).

#include "samples_folder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace sme::app {

QString samplesFolder() {
	const QString fromEnvironment = qEnvironmentVariable("SME_SAMPLES_DIR");
	if (!fromEnvironment.isEmpty() && QFileInfo(fromEnvironment).isDir()) return QDir(fromEnvironment).absolutePath();
	QDir dir(QCoreApplication::applicationDirPath());
	for (int up = 0; up < 4; ++up) {
		if (QFileInfo(dir.filePath(QStringLiteral("samples/README.txt"))).isFile()) return dir.absoluteFilePath(QStringLiteral("samples"));
		if (!dir.cdUp()) break;
	}
	return {};
}

QString samplesFolder(const char* part) {
	const QString root = samplesFolder();
	if (root.isEmpty()) return {};
	const QString sub = QDir(root).filePath(QString::fromLatin1(part));
	return QFileInfo(sub).isDir() ? sub : root;
}

} // namespace sme::app
