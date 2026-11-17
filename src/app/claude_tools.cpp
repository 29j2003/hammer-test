// claude_tools.cpp - The tools Claude can call through the Claude integration (claude_protocol.h
// lists them for Claude; claude_service.h checks access, rate and confirmation before any of them
// runs). Every name Claude sends goes through sme/claude_scope.h first; reads come from the open
// game's file system (never a path Claude chose), writes only reach the mod's decision file, the
// mod's own .vmt files and the exchange folder, each with a backup and an undo step.

#include "claude_service.h"

#include "claude_protocol.h"
#include "editor.h"
#include "material_decision_store.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <sme/editor_report.h>
#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/material.h>
#include <sme/material_decisions.h>
#include <sme/material_review.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <algorithm>

namespace sme::app {

using namespace sme::claude;

namespace {

QImage toQImage(const ImageRgba& image) {
	if (image.width == 0 || image.height == 0 || image.pixels.size() < std::size_t(image.width) * image.height * 4) return {};
	return QImage(reinterpret_cast<const uchar*>(image.pixels.data()), image.width, image.height, image.width * 4, QImage::Format_RGBA8888).copy();
}

/// A picture as JPEG (at most `longest` pixels on its longer side).
QByteArray jpeg(QImage image, int longest, int quality = 85) {
	if (image.isNull()) return {};
	if (std::max(image.width(), image.height()) > longest) image = image.scaled(longest, longest, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.convertToFormat(image.hasAlphaChannel() ? QImage::Format_RGB888 : image.format()).save(&buffer, "JPG", quality);
	return bytes;
}

QString str(const std::string& text) {
	return QString::fromStdString(text);
}

std::string utf8(const QString& text) {
	return text.toStdString();
}

/// A value of "set" as text (Claude sometimes sends numbers where the schema says string).
std::optional<QString> valueText(const QJsonValue& value) {
	if (value.isString()) return value.toString();
	if (value.isDouble()) return QString::number(value.toDouble(), 'g', 10);
	return std::nullopt;
}

QJsonObject refused(const QString& text) {
	return textResult(text, true);
}

bool writeText(const QString& path, const QByteArray& bytes) {
	QSaveFile out(path);
	return out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() && out.commit();
}

} // namespace

QString ClaudeService::masked(const QString& text) const {
	return str(maskPersonalText(utf8(text), homeFolder()));
}

std::set<std::string> ClaudeService::openMapMaterials() const {
	std::set<std::string> out;
	const auto& doc = m_editor->doc();
	const auto add = [&out](const Solid& solid) {
		for (const auto& side : solid.sides) {
			if (!side.material.empty()) out.insert(toLower(side.material));
		}
	};
	for (const auto& solid : doc.world.solids) add(solid);
	for (const auto& entity : doc.entities) {
		for (const auto& solid : entity.solids) add(solid);
	}
	return out;
}

const std::vector<std::string>& ClaudeService::worldMaterials() {
	if (m_materialsGame != m_editor->gameKey() || (m_materials.empty() && m_editor->game())) {
		m_materials.clear();
		m_materialsGame = m_editor->gameKey();
		if (const auto* game = m_editor->game()) {
			ReviewScope scope;
			scope.kind = ReviewScope::Kind::AllWorld;
			scope.decals = true;
			m_materials = reviewMaterialList(game->files, scope, openMapMaterials(), &m_editor->materialDecisions()->decisions());
		}
	}
	return m_materials;
}

QJsonObject ClaudeService::runTool(const QString& tool, const QJsonObject& a, QString& what) {
	if (tool == QStringLiteral("get_status")) return toolStatus();
	if (tool == QStringLiteral("list_exchange_files")) return toolListExchange();
	if (tool == QStringLiteral("read_exchange_file")) return toolReadExchange(a, what);
	if (tool == QStringLiteral("write_exchange_file")) return toolWriteExchange(a, what);
	if (tool == QStringLiteral("get_problems")) return toolProblems();
	if (tool == QStringLiteral("get_log")) return toolLog(a);
	if (tool == QStringLiteral("screenshot_viewport")) return toolScreenshot();
	if (tool == QStringLiteral("make_editor_report")) return toolEditorReport(what);
	// The rest needs an open game.
	if (!m_editor->game() || !m_editor->gameEntry()) return refused(tr("No game or mod is open in Mapsmith."));
	if (tool == QStringLiteral("list_materials")) return toolListMaterials(a);
	if (tool == QStringLiteral("get_material")) return toolGetMaterial(a, what);
	if (tool == QStringLiteral("get_material_pictures")) return toolMaterialPictures(a, what);
	if (tool == QStringLiteral("set_material_decision")) return toolSetDecision(a, what);
	if (tool == QStringLiteral("edit_material_parameter")) return toolEditMaterial(a, what);
	return refused(tr("Unknown tool."));
}

// --- Read --------------------------------------------------------------------------------------------

QJsonObject ClaudeService::toolStatus() {
	QStringList lines;
	lines << QStringLiteral("Mapsmith %1").arg(QStringLiteral(SME_VERSION));
	if (const auto* entry = m_editor->gameEntry()) {
		lines << QStringLiteral("Game or mod: %1").arg(str(entry->label.empty() ? entry->title : entry->label));
		lines << QStringLiteral("Mod folder: %1").arg(masked(QDir::toNativeSeparators(str(displayPath(entry->gameDir)))));
		// The full list reads every VMT of the game once (seconds on Half-Life 2): only when list_materials asks.
		if (m_materialsGame == m_editor->gameKey() && !m_materials.empty()) lines << QStringLiteral("World materials: %1").arg(m_materials.size());
		lines << QStringLiteral("Decision file: %1 lines (cfg/mapsmith_materials.txt)").arg(m_editor->materialDecisions()->decisions().entryCount());
		lines << QStringLiteral("mapbase-fidelity: %1").arg(m_editor->hasFidelity() ? QStringLiteral("yes") : QStringLiteral("no"));
	} else {
		lines << QStringLiteral("No game or mod is open.");
	}
	const QString map = m_editor->filePath();
	lines << QStringLiteral("Map: %1%2").arg(map.isEmpty() ? QStringLiteral("(not saved yet)") : QFileInfo(map).fileName(),
	                                        m_editor->modified() ? QStringLiteral(" (changed)") : QString());
	lines << QStringLiteral("Brushes: %1, entities: %2").arg(m_editor->doc().world.solids.size()).arg(m_editor->doc().entities.size());
	lines << QStringLiteral("Claude may: %1").arg(m_access == ClaudeAccess::Change ? QStringLiteral("read and change materials") : QStringLiteral("only read"));
	lines << QStringLiteral("Mapper confirms each change: %1").arg(m_ask ? QStringLiteral("yes") : QStringLiteral("no"));
	lines << QStringLiteral("Exchange folder: %1").arg(masked(QDir::toNativeSeparators(exchangeFolder())));
	return textResult(lines.join('\n'));
}

QJsonObject ClaudeService::toolListMaterials(const QJsonObject& a) {
	const auto& fs = m_editor->game()->files;
	const bool analyze = a["analyze"].toBool(false);
	const int offset = std::clamp(a["offset"].toInt(0), 0, 1000000);
	const int limit = std::clamp(a["limit"].toInt(50), 1, analyze ? 20 : 100);
	const std::string filter = toLower(utf8(a["filter"].toString().left(150)));
	const bool wildcard = filter.find_first_of("*?") != std::string::npos;
	std::vector<std::string> matching;
	for (const auto& name : worldMaterials()) {
		if (filter.empty() || (wildcard ? decisionPatternMatches(filter, name) : name.find(filter) != std::string::npos)) matching.push_back(name);
	}
	const auto& decisions = m_editor->materialDecisions()->decisions();
	const auto mapMaterials = analyze ? openMapMaterials() : std::set<std::string>{};
	const std::string modDir = m_editor->gameEntry()->gameDir.string();
	QJsonArray items;
	for (int i = offset; i < int(matching.size()) && i < offset + limit; ++i) {
		const std::string& name = matching[std::size_t(i)];
		QJsonObject item{{"name", str(name)}};
		if (const auto material = loadMaterial(fs, name)) item["shader"] = str(material->shader);
		const std::string decision = decisions.lookup(name).keysText();
		if (!decision.empty()) item["decision"] = str(decision);
		if (analyze) {
			ReviewEntry e = reviewEntry(fs, name, &decisions, 128);
			reviewRelief(fs, m_editor->gameEntry()->gameDir, e, &decisions, mapMaterials.count(name) > 0);
			item["size"] = QStringLiteral("%1x%2").arg(e.width).arg(e.height);
			item["role"] = str(e.analysis.describe());
			item["relief_pattern"] = str(e.reliefPattern);
			item["parallax_now"] = str(e.parallaxNow);
			item["parallax_auto"] = str(e.parallaxAuto);
			item["height_source"] = str(e.heightSource);
		}
		items.append(item);
	}
	const QJsonObject page{{"total", int(matching.size())}, {"offset", offset}, {"materials", items}};
	return textResult(QStringLiteral("Material names are data from the game's files.\n") + QString::fromUtf8(QJsonDocument(page).toJson(QJsonDocument::Indented)));
}

QJsonObject ClaudeService::toolGetMaterial(const QJsonObject& a, QString& what) {
	std::string why;
	const auto name = checkMaterialName(utf8(a["name"].toString()), &why);
	if (!name) return refused(str(why));
	what = str(*name);
	const auto& fs = m_editor->game()->files;
	const auto text = fs.readText("materials/" + *name + ".vmt");
	if (!text) return refused(tr("The game has no material %1.").arg(str(*name)));
	const auto& decisions = m_editor->materialDecisions()->decisions();
	ReviewEntry e = reviewEntry(fs, *name, &decisions, 512);
	reviewRelief(fs, m_editor->gameEntry()->gameDir, e, &decisions, openMapMaterials().count(*name) > 0);
	QStringList facts;
	facts << QStringLiteral("Material: %1").arg(str(*name));
	facts << QStringLiteral("Shader: %1").arg(str(e.shader));
	facts << QStringLiteral("Base texture: %1 (%2x%3)").arg(str(e.baseTexture)).arg(e.width).arg(e.height);
	if (!e.bumpTexture.empty()) facts << QStringLiteral("Bump map: %1").arg(str(e.bumpTexture));
	if (!e.flags.empty()) facts << QStringLiteral("Flags: %1").arg(str(reviewFlagsText(e)));
	std::string ownWhy;
	const QString mine = modMaterialFile(m_editor->gameEntry()->gameDir, *name, &ownWhy)
	                         ? QStringLiteral("yes (edit_material_parameter can change it)")
	                         : QStringLiteral("no (from a VPK or the base game: only the decision file can change how Mapsmith treats it)");
	facts << QStringLiteral("The mod's own file: %1").arg(mine);
	facts << QStringLiteral("Decision file: %1").arg(e.decision.empty() ? QStringLiteral("(no line)") : str(e.decision));
	facts << QStringLiteral("Mapsmith reads it as: %1").arg(str(e.analysis.describe()));
	facts << QStringLiteral("Relief pattern: %1").arg(str(e.reliefPattern));
	facts << QStringLiteral("Parallax now: %1").arg(str(e.parallaxNow));
	facts << QStringLiteral("Parallax automatic: %1").arg(str(e.parallaxAuto));
	facts << QStringLiteral("Relief heights from: %1 %2 (%3)").arg(str(e.heightSource), str(e.reliefDepth), str(e.reliefNote));
	facts << QStringLiteral("Bump map generated: %1").arg(e.bumpGenerated ? QStringLiteral("yes") : QStringLiteral("no"));
	if (!e.error.empty()) facts << QStringLiteral("Problem: %1").arg(str(e.error));
	QJsonArray content{textContent(facts.join('\n')), textContent(untrustedBlock(str(*name) + QStringLiteral(".vmt"), str(text->substr(0, 64 * 1024))))};
	if (a["picture"].toBool(true)) {
		const QByteArray picture = jpeg(toQImage(e.base), 512);
		if (!picture.isEmpty()) content.append(imageContent(picture));
	}
	return QJsonObject{{"content", content}, {"isError", false}};
}

QJsonObject ClaudeService::toolMaterialPictures(const QJsonObject& a, QString& what) {
	std::string why;
	const auto name = checkMaterialName(utf8(a["name"].toString()), &why);
	if (!name) return refused(str(why));
	what = str(*name);
	const auto& fs = m_editor->game()->files;
	if (!fs.exists("materials/" + *name + ".vmt")) return refused(tr("The game has no material %1.").arg(str(*name)));
	const auto& decisions = m_editor->materialDecisions()->decisions();
	ReviewEntry e = reviewEntry(fs, *name, &decisions, 64);
	ReviewPictures pictures;
	reviewRelief(fs, m_editor->gameEntry()->gameDir, e, &decisions, openMapMaterials().count(*name) > 0, &pictures);
	QJsonArray content;
	content.append(textContent(QStringLiteral("%1: base texture (up to 1024), relief heights from %2 (white = top), %3")
	                               .arg(str(*name), str(e.heightSource), pictures.normal.width ? QStringLiteral("its own normal map") : QStringLiteral("no own normal map"))));
	for (const auto* picture : {&pictures.base, &pictures.height, &pictures.normal}) {
		const QByteArray bytes = jpeg(toQImage(*picture), int(REVIEW_BASE_MAX));
		if (!bytes.isEmpty()) content.append(imageContent(bytes));
	}
	return QJsonObject{{"content", content}, {"isError", false}};
}

QJsonObject ClaudeService::toolProblems() {
	const QString text = m_host.problems ? m_host.problems() : QString();
	return textResult(untrustedBlock(QStringLiteral("problems"), masked(text.isEmpty() ? QStringLiteral("(no problems listed)") : text.right(200000))));
}

QJsonObject ClaudeService::toolLog(const QJsonObject& a) {
	const bool compile = a["compile"].toBool(false);
	const int count = std::clamp(a["lines"].toInt(100), 1, 500);
	const QString text = compile ? (m_host.compileLog ? m_host.compileLog() : QString()) : (m_host.log ? m_host.log() : QString());
	QStringList lines = text.split('\n');
	if (lines.size() > count) lines = lines.mid(lines.size() - count);
	return textResult(untrustedBlock(compile ? QStringLiteral("compile log") : QStringLiteral("Mapsmith log"), masked(lines.join('\n').right(200000))));
}

QJsonObject ClaudeService::toolScreenshot() {
	const QImage image = m_host.screenshot ? m_host.screenshot() : QImage();
	const QByteArray bytes = jpeg(image, 1280, 80);
	if (bytes.isEmpty()) return refused(tr("No 3D view to take a picture of."));
	return QJsonObject{{"content", QJsonArray{imageContent(bytes)}}, {"isError", false}};
}

QJsonObject ClaudeService::toolListExchange() {
	const QString root = exchangeFolder();
	QDir().mkpath(root);
	QStringList files;
	QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
	while (it.hasNext() && files.size() < 500) {
		it.next();
		const QFileInfo info = it.fileInfo();
		files << QStringLiteral("%1  (%2 bytes)").arg(QDir(root).relativeFilePath(info.filePath())).arg(info.size());
	}
	files.sort();
	return textResult(files.isEmpty() ? QStringLiteral("The exchange folder is empty.") : files.join('\n'));
}

QJsonObject ClaudeService::toolReadExchange(const QJsonObject& a, QString& what) {
	const QString root = exchangeFolder();
	QDir().mkpath(root);
	std::string why;
	const auto path = resolveInside(root.toStdWString(), utf8(a["name"].toString()), &why);
	if (!path) return refused(str(why));
	what = a["name"].toString().left(200);
	QFile file(QString::fromStdWString(path->wstring()));
	if (!QFileInfo(file).isFile()) return refused(tr("There is no such file in the exchange folder."));
	if (file.size() > qint64(CLAUDE_READ_MAX)) return refused(tr("The file is larger than 1 MB."));
	if (!file.open(QIODevice::ReadOnly)) return refused(tr("The file could not be read."));
	const QByteArray bytes = file.readAll();
	if (bytes.contains('\0')) return refused(tr("This is not a text file."));
	return textResult(untrustedBlock(what, QString::fromUtf8(bytes)));
}

// --- Write -------------------------------------------------------------------------------------------

QJsonObject ClaudeService::toolSetDecision(const QJsonObject& a, QString& what) {
	auto* store = m_editor->materialDecisions();
	const QString pattern = a["material"].toString().trimmed();
	what = pattern.left(150);
	std::vector<std::pair<std::string, std::string>> keys;
	const QJsonObject set = a["set"].toObject();
	for (auto it = set.begin(); it != set.end(); ++it) {
		const auto value = valueText(it.value());
		if (!value) return refused(tr("The value of %1 must be text.").arg(it.key().left(40)));
		keys.emplace_back(utf8(it.key()), utf8(*value));
	}
	static const std::set<std::string> removable = {"role", "windows", "floors", "windowrects", "scale", "parallax", "depth", "pattern", "bump", "bumpstrength", "note"};
	std::vector<std::string> removals;
	for (const auto& value : a["remove"].toArray()) {
		const std::string key = toLower(utf8(value.toString()));
		if (!removable.count(key)) return refused(tr("Unknown key to remove: %1").arg(value.toString().left(40)));
		removals.push_back(key);
	}
	if (keys.empty() && removals.empty()) return refused(tr("Nothing to change: give \"set\" or \"remove\"."));
	std::string why;
	std::optional<std::string> line;
	if (!keys.empty()) {
		line = decisionLineFromClaude(utf8(pattern), keys, &why);
		if (!line) return refused(str(why));
	} else if (!decisionLineFromClaude(utf8(pattern), {{"note", "x"}}, &why)) {
		return refused(str(why));   // the same name check for removals
	}
	QStringList removed;
	for (const auto& r : removals) removed << str(r);
	QString description = pattern;
	if (line) description += QStringLiteral(": ") + str(line->substr(utf8(pattern).size() + 1));
	if (!removed.isEmpty()) description += QStringLiteral(" (remove %1)").arg(removed.join(QStringLiteral(", ")));
	if (m_ask && m_host.confirm && !m_host.confirm(tr("Change a material decision"), description)) return refused(tr("The mapper declined this change."));
	QString error;
	const QString file = store->filePath();
	const QString copy = backup(file, &error);
	if (!error.isEmpty()) return refused(tr("No backup, so nothing was changed: %1").arg(error));
	const bool ok = store->change([&](MaterialDecisions& decisions) {
		if (line) decisions.merge(MaterialDecisions::parse(*line));
		if (!removals.empty()) {
			decisions.edit(utf8(pattern), [&](MaterialDecision& d) {
				for (const auto& key : removals) {
					if (key == "role") d.role.reset();
					else if (key == "windows") d.windowColumns.reset(), d.windowRows.reset();
					else if (key == "floors") d.floors.reset();
					else if (key == "windowrects") d.windowRects.reset();
					else if (key == "scale") d.scale.reset();
					else if (key == "parallax") d.parallax.reset();
					else if (key == "depth") d.depth.reset();
					else if (key == "pattern") d.pattern.reset();
					else if (key == "bump") d.bump.reset();
					else if (key == "bumpstrength") d.bumpStrength.reset();
					else if (key == "note") d.note.reset();
				}
			});
		}
	});
	if (!ok) return refused(tr("The decision file could not be written."));
	what = description;
	pushUndo({file, copy, tr("Decision %1").arg(description), true, {}});
	const auto* exact = store->decisions().exact(decisionKey(utf8(pattern)));
	const std::string now = exact ? exact->keysText() : std::string();
	return textResult(tr("Changed. The line for %1 now says: %2. The mapper can undo this in Project > Claude.").arg(pattern, now.empty() ? QStringLiteral("(nothing)") : str(now)));
}

QJsonObject ClaudeService::toolEditMaterial(const QJsonObject& a, QString& what) {
	std::string why;
	const auto name = checkMaterialName(utf8(a["material"].toString()), &why);
	if (!name) return refused(str(why));
	const auto file = modMaterialFile(m_editor->gameEntry()->gameDir, *name, &why);
	if (!file) return refused(str(why));
	const auto key = checkVmtParameter(utf8(a["parameter"].toString()), &why);
	if (!key) return refused(str(why));
	const bool remove = a["remove"].toBool(false);
	const auto value = valueText(a["value"]);
	if (!remove && !value) return refused(tr("Give a value (or remove: true)."));
	if (!remove && !checkVmtValue(utf8(*value), &why)) return refused(str(why));
	what = QStringLiteral("%1 %2 = %3").arg(str(*name), str(*key), remove ? QStringLiteral("(removed)") : *value);
	const QString path = QString::fromStdWString(file->wstring());
	QFile in(path);
	if (in.size() > 256 * 1024 || !in.open(QIODevice::ReadOnly)) return refused(tr("The material file is too large or cannot be read."));
	const std::string text = in.readAll().toStdString();
	in.close();
	const auto material = parseMaterial(text, *name);
	if (!material) return refused(tr("The material file cannot be read (broken syntax) - nothing changed."));
	std::string edited;
	try {
		edited = editMaterialText(text, material->shader, {MaterialEdit{*key, remove ? std::nullopt : std::optional<std::string>(utf8(*value))}});
	} catch (const std::exception& e) {
		return refused(tr("The material file cannot be changed: %1").arg(QString::fromUtf8(e.what())));
	}
	if (edited == text) return textResult(tr("Nothing to change: %1 already says that.").arg(str(*name)));
	// The result must still be a material with the same shader (a value cannot open a new block).
	const auto check = parseMaterial(edited, *name);
	if (!check || toLower(check->shader) != toLower(material->shader)) return refused(tr("The change would break the material - nothing changed."));
	if (m_ask && m_host.confirm && !m_host.confirm(tr("Change a material"), what)) return refused(tr("The mapper declined this change."));
	QString error;
	const QString copy = backup(path, &error);
	if (copy.isEmpty()) return refused(tr("No backup, so nothing was changed: %1").arg(error.isEmpty() ? tr("unknown") : error));
	if (!writeText(path, QByteArray::fromStdString(edited))) return refused(tr("The material file could not be written."));
	pushUndo({path, copy, tr("Material %1").arg(what), false, str(*name)});
	if (m_host.materialChanged) m_host.materialChanged(str(*name));
	return textResult(tr("Changed %1. Mapsmith shows it now; the mapper can undo it in Project > Claude.").arg(what));
}

QJsonObject ClaudeService::toolWriteExchange(const QJsonObject& a, QString& what) {
	const QString root = exchangeFolder();
	if (!QDir().mkpath(root)) return refused(tr("The exchange folder could not be made."));
	const QString name = a["name"].toString();
	std::string why;
	const auto checked = checkExchangeName(utf8(name), &why);
	if (!checked) return refused(str(why));
	if (!exchangeExtensionAllowed(*checked)) return refused(tr("Only text files: .txt .md .csv .json .log .vmt"));
	if (toLower(*checked).rfind("backups/", 0) == 0) return refused(tr("The backups folder is Mapsmith's own."));
	const QByteArray bytes = a["text"].toString().toUtf8();
	if (std::size_t(bytes.size()) > CLAUDE_WRITE_MAX) return refused(tr("The text is larger than 1 MB."));
	const auto path = resolveInside(root.toStdWString(), *checked, &why);
	if (!path) return refused(str(why));
	what = str(*checked);
	if (m_ask && m_host.confirm && !m_host.confirm(tr("Write a file in the exchange folder"), what)) return refused(tr("The mapper declined this change."));
	const QString target = QString::fromStdWString(path->wstring());
	if (!QDir().mkpath(QFileInfo(target).absolutePath())) return refused(tr("The folder could not be made."));
	// The folder made just now must still be inside (nothing swapped in a link meanwhile).
	if (!pathInside(root.toStdWString(), path->parent_path())) return refused(tr("The name leads outside the exchange folder."));
	QString error;
	const QString copy = backup(target, &error);
	if (!error.isEmpty()) return refused(tr("No backup, so nothing was changed: %1").arg(error));
	if (!writeText(target, bytes)) return refused(tr("The file could not be written."));
	pushUndo({target, copy, tr("File %1").arg(what), false, {}});
	return textResult(tr("Wrote %1 (%2 bytes) in the exchange folder.").arg(what).arg(bytes.size()));
}

QJsonObject ClaudeService::toolEditorReport(QString& what) {
	if (!m_host.writeReport) return refused(tr("Mapsmith cannot write a report here."));
	const QString folder = exchangeFolder() + QStringLiteral("/reports");
	if (!QDir().mkpath(folder)) return refused(tr("The reports folder could not be made."));
	const QString name = QStringLiteral("mapsmith_report_%1.zip").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
	what = QStringLiteral("reports/") + name;
	if (m_ask && m_host.confirm && !m_host.confirm(tr("Make an editor report"), what)) return refused(tr("The mapper declined this."));
	QString error;
	if (!m_host.writeReport(QDir(folder).filePath(name), &error)) return refused(tr("The report could not be written: %1").arg(error));
	return textResult(tr("Wrote the editor report %1 in the exchange folder (personal folders are masked). It is a ZIP: the mapper can send it on.").arg(what));
}

} // namespace sme::app
