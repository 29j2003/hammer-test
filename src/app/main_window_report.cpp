// main_window_report.cpp - The editor report: Help > "Make editor report ..." writes a ZIP
// with everything needed to see (and replay) what the editor did - the map around the selection,
// the editor's state, the last clicks with what the pick saw, the last edits and road plans, the
// Log and Problems texts and a picture of each view (see sme/editor_report.h). Nothing is sent
// anywhere: the file is written where he chooses and he sends it himself. Paths are masked.

#include "main_window.h"

#include "editor.h"
#include "panels.h"
#include "road_dialog.h"
#include "texture_flow_dialog.h"
#include "viewport.h"

#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#include <algorithm>
#include <cstring>

#include <sme/editing.h>
#include <sme/editor_report.h>
#include <sme/shipping.h>

#ifndef SME_COMMIT
#define SME_COMMIT "unknown"
#endif

namespace sme::app {

namespace {

constexpr double REPORT_MARGIN = 1024;                       // units around the selection kept in scene.vmf
constexpr std::size_t REPORT_WHOLE_MAP = 5u * 1024u * 1024u;   // smaller maps go in whole

std::vector<std::byte> bytesOf(const std::string& text) {
	std::vector<std::byte> out(text.size());
	std::transform(text.begin(), text.end(), out.begin(), [](char c) { return static_cast<std::byte>(c); });
	return out;
}

QJsonArray vecJson(const Vec3& v) {
	return QJsonArray{v.x, v.y, v.z};
}

} // namespace

bool MainWindow::writeEditorReport(const QString& path, QString* error) {
	const std::string home = homeFolder();
	const auto mask = [&home](const std::string& text) { return maskPersonalText(text, home); };
	const auto& doc = m_editor->doc();
	const auto& selection = m_editor->selection();
	std::vector<PackageEntry> entries;
	const auto add = [&](const std::string& name, const std::string& text) { entries.push_back({name, {}, bytesOf(mask(text))}); };

	// The map around the selection (or around the 3D camera), ids kept.
	Bounds area = edit::selectionBounds(doc, selection, m_editor->entities());
	for (const auto& face : selection.faces) {
		if (const Solid* solid = doc.findSolid(face.solid)) area.add(buildBrushGeometry(*solid).bounds);
	}
	const Bounds selected = area;
	if (Viewport* view = view3D()) {
		const QVector3D p = view->camera().position;
		area.add(Vec3{p.x(), p.y(), p.z()});
	}
	std::size_t keptSolids = 0, keptEntities = 0;
	const MapDocument scene = reportScene(doc, area, REPORT_MARGIN, REPORT_WHOLE_MAP, &keptSolids, &keptEntities);
	add("scene.vmf", scene.serialize());

	// The editor's state, for people.
	QJsonObject info;
	info["version"] = QApplication::applicationVersion();
	info["commit"] = QStringLiteral(SME_COMMIT);
	info["qt"] = QString::fromLatin1(qVersion());
	info["os"] = QSysInfo::prettyProductName() + " (" + QSysInfo::kernelType() + " " + QSysInfo::kernelVersion() + ", " + QSysInfo::currentCpuArchitecture() + ")";
	if (Viewport* view = view3D()) info["gpu"] = view->glInfo();
	if (const QScreen* screen = this->screen()) {
		info["screen"] = QJsonObject{{"dpi", screen->logicalDotsPerInch()}, {"devicePixelRatio", screen->devicePixelRatio()},
		                             {"size", QStringLiteral("%1x%2").arg(screen->size().width()).arg(screen->size().height())}};
	}
	info["game"] = m_editor->gameKey();
	info["map"] = QFileInfo(m_editor->filePath()).fileName();
	info["mapBrushes"] = static_cast<qint64>(doc.solidCount());
	info["mapEntities"] = static_cast<qint64>(doc.entities.size());
	info["sceneBrushes"] = static_cast<qint64>(keptSolids);
	info["sceneEntities"] = static_cast<qint64>(keptEntities);
	info["sceneArea"] = QJsonObject{{"min", vecJson(area.min)}, {"max", vecJson(area.max)}, {"margin", REPORT_MARGIN}};
	info["layout"] = m_quad ? "four views" : "one view";
	QJsonArray views;
	for (int i = 0; i < 4; ++i) {
		Viewport* view = m_views[static_cast<std::size_t>(i)];
		if (!view) continue;
		const Camera& c = view->camera();
		views.append(QJsonObject{{"index", i},
		                         {"kind", view->kindName()},
		                         {"visible", view->isVisible()},
		                         {"size", QStringLiteral("%1x%2").arg(view->width()).arg(view->height())},
		                         {"position", QJsonArray{c.position.x(), c.position.y(), c.position.z()}},
		                         {"yaw", c.yaw},
		                         {"pitch", c.pitch},
		                         {"fov", c.fov},
		                         {"zoom", c.zoom}});
	}
	info["views"] = views;
	info["grid"] = m_editor->grid();
	info["snap"] = m_editor->snapGrid();
	info["objectSnap"] = m_editor->objectSnap();
	info["textureLock"] = m_editor->textureLock();
	info["tool"] = QString::fromLatin1(Editor::toolName(m_editor->tool()));
	info["selectMode"] = m_editor->selectMode() == Editor::SelectMode::Face ? "faces" : "objects";
	info["selection"] = QJsonObject{{"brushes", static_cast<qint64>(selection.solids.size())},
	                                {"entities", static_cast<qint64>(selection.entities.size())},
	                                {"faces", static_cast<qint64>(selection.faces.size())},
	                                {"box", selected.empty() ? QJsonValue() : QJsonValue(QJsonObject{{"min", vecJson(selected.min)}, {"max", vecJson(selected.max)}})}};
	const Selection& hidden = m_editor->hidden();
	info["hidden"] = QJsonObject{{"kinds", static_cast<qint64>(m_editor->hiddenKinds())},
	                             {"brushes", static_cast<qint64>(hidden.solids.size())},
	                             {"entities", static_cast<qint64>(hidden.entities.size())},
	                             {"faces", static_cast<qint64>(hidden.faces.size())},
	                             {"visgroups", static_cast<qint64>(m_editor->hiddenVisGroups().size())}};
	info["livePreviewBrushes"] = static_cast<qint64>(m_editor->brushPreview().size());
	std::vector<std::pair<std::string, std::string>> replay = m_editor->replaySettings();
	QJsonObject dialogs;
	if (m_roadDialog && m_roadDialog->isVisible()) {
		const RoadOptions o = m_roadDialog->options();
		dialogs["road"] = QJsonObject{{"material", QString::fromStdString(o.material)},
		                              {"along", o.along == RoadAlong::Auto ? "auto" : o.along == RoadAlong::U ? "U" : "V"},
		                              {"flip", o.flip},
		                              {"repeatsAcross", o.repeatsAcross},
		                              {"width", o.roadWidth},
		                              {"curveCorners", o.curveCorners},
		                              {"kerbMaterial", QString::fromStdString(o.sideMaterial)}};
	}
	if (m_flowDialog && m_flowDialog->isVisible()) {
		std::vector<std::pair<std::string, std::string>> flow;
		addFlowSettings(flow, m_flowDialog->options(), {0, 0});
		QJsonObject o;
		for (const auto& [key, value] : flow) o[QString::fromStdString(key)] = QString::fromStdString(value);
		dialogs["textureFlow"] = o;
	}
	info["openDialogs"] = dialogs;
	add("editor.json", QJsonDocument(info).toJson(QJsonDocument::Indented).toStdString());

	// The state the replay needs.
	for (int i = 0; i < 4; ++i) {
		Viewport* view = m_views[static_cast<std::size_t>(i)];
		if (!view) continue;
		const Camera& c = view->camera();
		replay.emplace_back("view" + std::to_string(i), view->kindName().toStdString() + " " + std::to_string(view->width()) + "x" + std::to_string(view->height()) +
		                                                     " " + reportNumber(c.position.x()) + " " + reportNumber(c.position.y()) + " " +
		                                                     reportNumber(c.position.z()) + " yaw " + reportNumber(c.yaw) + " pitch " + reportNumber(c.pitch) +
		                                                     " fov " + reportNumber(c.fov) + " zoom " + reportNumber(c.zoom));
	}
	add("replay.txt", formatSettings(replay));
	add("picks.log", m_editor->pickJournal());
	add("edits.log", m_editor->editJournal());
	add("road.log", m_editor->planJournal());
	// After a crash: the clicks and edits of the session that crashed (autosaver.h).
	if (!m_crashJournal.isEmpty()) add("last_session.log", m_crashJournal.toStdString());
	add("log.txt", m_log->toPlainText().toStdString());
	add("problems.txt", m_problems->problemsText().toStdString());
	add("README.txt", "Mapsmith editor report. Replay: sme-probe editor-report <this zip> (docs/ENTWICKLUNG.md, \"Editor-Bericht\").\n");

	// A picture of each view as it is now.
	for (int i = 0; i < 4; ++i) {
		Viewport* view = m_views[static_cast<std::size_t>(i)];
		if (!view || !view->isVisible()) continue;
		const QImage shot = view->grabFramebuffer();
		if (shot.isNull()) continue;
		QByteArray png;
		QBuffer buffer(&png);
		buffer.open(QIODevice::WriteOnly);
		shot.save(&buffer, "PNG");
		PackageEntry entry;
		entry.name = "view" + std::to_string(i) + "_" + view->kindName().toLower().toStdString() + ".png";
		entry.data.resize(static_cast<std::size_t>(png.size()));
		std::memcpy(entry.data.data(), png.constData(), entry.data.size());
		entries.push_back(std::move(entry));
	}

	std::string message;
	if (!writeZip(QDir::toNativeSeparators(path).toStdWString(), entries, &message)) {
		if (error) *error = QString::fromStdString(message);
		return false;
	}
	return true;
}

void MainWindow::makeEditorReport() {
	QString folder = QSettings().value("report/folder").toString();
	if (folder.isEmpty() || !QDir(folder).exists()) folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	if (folder.isEmpty() || !QDir(folder).exists()) folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	const QString name = QStringLiteral("mapsmith_report_%1.zip").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmm"));
	// Not modal: the views keep their state (and the dialog can be found by tests).
	auto* dialog = new QFileDialog(this, tr("Save editor report"), QDir(folder).filePath(name), tr("ZIP files (*.zip)"));
	dialog->setObjectName("editorReportFile");
	dialog->setAcceptMode(QFileDialog::AcceptSave);
	dialog->setDefaultSuffix("zip");
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	connect(dialog, &QFileDialog::fileSelected, this, [this](const QString& path) {
		if (path.isEmpty()) return;
		QSettings().setValue("report/folder", QFileInfo(path).absolutePath());
		QString error;
		auto* box = new QMessageBox(this);
		box->setObjectName("editorReportDone");
		box->setAttribute(Qt::WA_DeleteOnClose);
		box->setWindowTitle(tr("Editor report"));
		if (writeEditorReport(path, &error)) {
			box->setIcon(QMessageBox::Information);
			box->setText(tr("Report saved - send this file:"));
			box->setInformativeText(QDir::toNativeSeparators(path) +
			                        tr("\n\nIt holds the map around the selection, the editor's settings, your last clicks and edits and a picture of each "
			                           "view. Nothing was sent anywhere."));
			auto* open = box->addButton(tr("Open folder"), QMessageBox::ActionRole);
			open->setObjectName("editorReportOpen");
			const QString dir = QFileInfo(path).absolutePath();
			connect(open, &QPushButton::clicked, this, [dir] { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); });
			box->addButton(QMessageBox::Close);
			m_log->append(tr("Editor report saved: %1").arg(QDir::toNativeSeparators(path)));
		} else {
			box->setIcon(QMessageBox::Warning);
			box->setText(tr("The report could not be written: %1").arg(error));
			box->addButton(QMessageBox::Close);
		}
		box->open();
	});
	dialog->open();
}

} // namespace sme::app
