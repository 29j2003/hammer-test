// ai_upscaler.cpp - Runs Real-ESRGAN ncnn Vulkan on one picture (see ai_upscaler.h).

#include "ai_upscaler.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTemporaryDir>

#include <sme/asset_import.h>
#include <sme/texture.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace sme::app {

QString realEsrganFolder() {
	const QString program = QString::fromStdString(realEsrganProgramName());
	const auto usable = [&program](const QString& dir) {
		const QFileInfo exe(QDir(dir).filePath(program));
		const QFileInfo model(QDir(dir).filePath(QStringLiteral("models/%1.param").arg(QString::fromLatin1(REAL_ESRGAN_MODEL.data(), int(REAL_ESRGAN_MODEL.size())))));
		return exe.isFile() && exe.isExecutable() && model.isFile();
	};
	const QString override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("SME_REALESRGAN_DIR"));
	if (!override.isEmpty() && usable(override)) return QDir(override).absolutePath();
	const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QString::fromLatin1(REAL_ESRGAN_FOLDER.data(), int(REAL_ESRGAN_FOLDER.size())));
	return usable(bundled) ? QDir(bundled).absolutePath() : QString();
}

AiUpscaler makeAiUpscaler() {
	const QString folder = realEsrganFolder();
	if (folder.isEmpty()) return {};
	return [folder](const ImageRgba& picture, unsigned factor, const std::atomic<bool>* cancel, std::string* error) -> std::optional<ImageRgba> {
		const auto fail = [error](const QString& why) -> std::optional<ImageRgba> {
			if (error) *error = why.toStdString();
			return std::nullopt;
		};
		if (factor != 4) return fail(QStringLiteral("the AI upscaler makes pictures 4 times larger only"));
		// A folder of Mapsmith's own for the two pictures (removed afterwards).
		QTemporaryDir temp(QDir::temp().filePath(QStringLiteral("mapsmith-upscale-XXXXXX")));
		if (!temp.isValid()) return fail(QStringLiteral("no temporary folder"));
		const QString input = temp.filePath(QStringLiteral("in.png"));
		const QString output = temp.filePath(QStringLiteral("out.png"));
		{
			const auto png = encodePng(picture);
			QFile file(input);
			if (png.empty() || !file.open(QIODevice::WriteOnly) || file.write(reinterpret_cast<const char*>(png.data()), qint64(png.size())) != qint64(png.size())) {
				return fail(QStringLiteral("the picture could not be written"));
			}
		}
		QStringList arguments;
		for (const auto& argument :
		     realEsrganArguments(input.toStdString(), output.toStdString(), QDir(folder).filePath(QStringLiteral("models")).toStdString())) {
			arguments << QString::fromStdString(argument);
		}
		QProcess process;
		process.setProcessChannelMode(QProcess::MergedChannels);
		process.setWorkingDirectory(temp.path());
#ifdef Q_OS_WIN
		process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
#endif
		// The full path: never a program of the same name from PATH or the current folder.
		process.start(QDir(folder).filePath(QString::fromStdString(realEsrganProgramName())), arguments);
		if (!process.waitForStarted(10000)) return fail(QStringLiteral("the AI upscaler did not start: %1").arg(process.errorString()));
		QElapsedTimer clock;
		clock.start();
		QByteArray log;
		while (!process.waitForFinished(100)) {
			log += process.readAll();
			if ((cancel && cancel->load()) || clock.elapsed() > 10 * 60 * 1000) {
				process.kill();
				process.waitForFinished(2000);
				return fail(cancel && cancel->load() ? QStringLiteral("cancelled") : QStringLiteral("the AI upscaler took longer than 10 minutes"));
			}
			if (process.state() == QProcess::NotRunning) break;
		}
		log += process.readAll();
		QFile file(output);
		if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !file.open(QIODevice::ReadOnly)) {
			// Its last line says why (no Vulkan device, out of memory ...).
			QString last;
			for (const auto& line : QString::fromLocal8Bit(log).split('\n', Qt::SkipEmptyParts)) {
				if (!line.trimmed().isEmpty()) last = line.trimmed();
			}
			return fail(QStringLiteral("the AI upscaler failed%1").arg(last.isEmpty() ? QString() : QStringLiteral(": ") + last.left(200)));
		}
		const QByteArray bytes = file.readAll();
		std::string why;
		auto image = decodeImageFile(std::span(reinterpret_cast<const std::byte*>(bytes.constData()), std::size_t(bytes.size())), &why);
		if (!image) return fail(QStringLiteral("the AI upscaler's picture cannot be read: %1").arg(QString::fromStdString(why)));
		return image;
	};
}

} // namespace sme::app
