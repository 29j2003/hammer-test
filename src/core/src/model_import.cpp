// model_import.cpp - OBJ/SMD to studiomdl sources (see model_import.h).

#include <sme/model_import.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/map.h>
#include <sme/text.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <tuple>

namespace sme {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t MAX_TRIANGLES = 2'000'000;   // far beyond any prop; stops absurd files early
constexpr std::size_t MAX_VERTICES = 4'000'000;

double toNumber(std::string_view text, bool* ok = nullptr) {
	text = trim(text);
	double value = 0;
	const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	const bool good = ec == std::errc() && std::isfinite(value);
	if (ok) *ok = good;
	return good ? value : 0;
}

std::vector<std::string_view> words(std::string_view line) {
	std::vector<std::string_view> out;
	std::size_t at = 0;
	while (at < line.size()) {
		while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
		const std::size_t start = at;
		while (at < line.size() && line[at] != ' ' && line[at] != '\t') ++at;
		if (at > start) out.push_back(line.substr(start, at - start));
	}
	return out;
}

std::vector<std::string_view> lines(std::string_view text) {
	std::vector<std::string_view> out;
	std::size_t at = 0;
	while (at <= text.size()) {
		auto end = text.find('\n', at);
		if (end == std::string_view::npos) end = text.size();
		auto line = text.substr(at, end - at);
		if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
		out.push_back(line);
		if (end == text.size()) break;
		at = end + 1;
	}
	return out;
}

Vec3 faceNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
	const Vec3 n = cross(b - a, c - a);
	const double l = length(n);
	return l > 1e-12 ? n / l : Vec3{0, 0, 1};
}

/// The rest of a line after its keyword (names with spaces: "map_Kd my texture.png").
std::string afterKeyword(std::string_view line, std::string_view keyword) {
	line = trim(line);
	return std::string(trim(line.substr(std::min(line.size(), keyword.size()))));
}

void noteMaterial(ImportMesh& mesh, const std::string& material) {
	if (std::find(mesh.materials.begin(), mesh.materials.end(), material) == mesh.materials.end()) mesh.materials.push_back(material);
}

std::string number(double v) {
	if (std::abs(v) < 1e-9) v = 0;
	char buffer[32];
	const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), v, std::chars_format::fixed, 6);
	std::string out(buffer, ec == std::errc() ? end : buffer);
	while (out.size() > 1 && out.back() == '0') out.pop_back();
	if (!out.empty() && out.back() == '.') out.pop_back();
	return out == "-0" ? "0" : out;
}

bool writeText(const fs::path& file, std::string_view text, std::string* error) {
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	if (!out) {
		if (error) *error = "cannot write " + displayPath(file);
		return false;
	}
	return true;
}

} // namespace

Bounds ImportMesh::bounds() const {
	Bounds box;
	for (const auto& t : triangles) {
		for (const auto& c : t.corners) box.add(c.position);
	}
	return box;
}

std::string objMaterialLibrary(std::string_view obj) {
	for (const auto line : lines(obj)) {
		const auto w = words(line);
		if (w.size() >= 2 && w[0] == "mtllib") return afterKeyword(line, "mtllib");
	}
	return {};
}

std::optional<ImportMesh> readObj(std::string_view obj, std::string_view mtl, std::string* error) {
	ImportMesh mesh;
	// Material library: picture and colour per material.
	std::string currentMtl;
	for (const auto line : lines(mtl)) {
		const auto w = words(line);
		if (w.empty()) continue;
		if (w[0] == "newmtl" && w.size() >= 2) {
			currentMtl = afterKeyword(line, "newmtl");
		} else if (!currentMtl.empty() && w[0] == "map_Kd" && w.size() >= 2) {
			// Options like "-s 1 1 1" before the file name are skipped: the name is the last word(s).
			std::string file = afterKeyword(line, "map_Kd");
			if (!file.empty() && file.front() == '-') file = std::string(w.back());
			mesh.textures[currentMtl] = file;
		} else if (!currentMtl.empty() && w[0] == "Kd" && w.size() >= 4) {
			mesh.colors[currentMtl] = {std::clamp(toNumber(w[1]), 0.0, 1.0), std::clamp(toNumber(w[2]), 0.0, 1.0), std::clamp(toNumber(w[3]), 0.0, 1.0)};
		}
	}
	std::vector<Vec3> positions, normals;
	std::vector<std::pair<double, double>> uvs;
	std::string material = "default";
	// "a/b/c" -> indices (0-based; -1 missing); negative OBJ indices count from the end.
	const auto index = [](std::string_view part, std::size_t count) -> long long {
		if (part.empty()) return -1;
		long long value = 0;
		const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), value);
		if (ec != std::errc() || value == 0) return -2;
		const long long resolved = value > 0 ? value - 1 : static_cast<long long>(count) + value;
		return resolved >= 0 && resolved < static_cast<long long>(count) ? resolved : -2;
	};
	for (const auto line : lines(obj)) {
		const auto w = words(line);
		if (w.empty() || w[0].starts_with('#')) continue;
		if (w[0] == "v" && w.size() >= 4) {
			if (positions.size() >= MAX_VERTICES) continue;
			positions.push_back({toNumber(w[1]), toNumber(w[2]), toNumber(w[3])});
		} else if (w[0] == "vn" && w.size() >= 4) {
			if (normals.size() >= MAX_VERTICES) continue;
			normals.push_back({toNumber(w[1]), toNumber(w[2]), toNumber(w[3])});
		} else if (w[0] == "vt" && w.size() >= 2) {
			if (uvs.size() >= MAX_VERTICES) continue;
			uvs.emplace_back(toNumber(w[1]), w.size() >= 3 ? toNumber(w[2]) : 0.0);
		} else if (w[0] == "usemtl" && w.size() >= 2) {
			material = afterKeyword(line, "usemtl");
		} else if (w[0] == "f" && w.size() >= 4) {
			std::vector<ImportMesh::Vertex> corners;
			bool broken = false;
			bool hasNormals = true;
			for (std::size_t i = 1; i < w.size(); ++i) {
				const auto part = w[i];
				const auto s1 = part.find('/');
				const auto s2 = s1 == std::string_view::npos ? std::string_view::npos : part.find('/', s1 + 1);
				const long long p = index(part.substr(0, s1), positions.size());
				const long long t = s1 == std::string_view::npos ? -1 : index(part.substr(s1 + 1, s2 == std::string_view::npos ? std::string_view::npos : s2 - s1 - 1), uvs.size());
				const long long n = s2 == std::string_view::npos ? -1 : index(part.substr(s2 + 1), normals.size());
				if (p < 0 || t == -2 || n == -2) {
					broken = true;
					break;
				}
				ImportMesh::Vertex v;
				v.position = positions[std::size_t(p)];
				if (t >= 0) std::tie(v.u, v.v) = uvs[std::size_t(t)];
				if (n >= 0) v.normal = normalize(normals[std::size_t(n)]);
				else hasNormals = false;
				corners.push_back(v);
			}
			if (broken) continue;
			for (std::size_t i = 1; i + 1 < corners.size() && mesh.triangles.size() < MAX_TRIANGLES; ++i) {
				ImportMesh::Triangle tri{{corners[0], corners[i], corners[i + 1]}, material};
				if (!hasNormals) {
					const Vec3 normal = faceNormal(tri.corners[0].position, tri.corners[1].position, tri.corners[2].position);
					for (auto& c : tri.corners) c.normal = normal;
				}
				mesh.triangles.push_back(tri);
				noteMaterial(mesh, material);
			}
		}
	}
	if (mesh.triangles.empty()) {
		if (error) *error = "the .obj has no faces";
		return std::nullopt;
	}
	return mesh;
}

std::optional<ImportMesh> readSmd(std::string_view smd, std::string* error) {
	ImportMesh mesh;
	const auto all = lines(smd);
	bool inTriangles = false;
	for (std::size_t i = 0; i < all.size(); ++i) {
		const auto line = trim(all[i]);
		if (!inTriangles) {
			inTriangles = line == "triangles";
			continue;
		}
		if (line == "end") break;
		if (line.empty() || line.starts_with("//")) continue;
		if (i + 3 >= all.size() || mesh.triangles.size() >= MAX_TRIANGLES) break;
		ImportMesh::Triangle tri;
		tri.material = std::string(line);
		// Strip a picture extension: studiomdl uses the name without it.
		if (const auto dot = tri.material.rfind('.'); dot != std::string::npos && tri.material.find('/', dot) == std::string::npos) tri.material.resize(dot);
		bool ok = true;
		for (int c = 0; c < 3; ++c) {
			const auto w = words(all[i + 1 + std::size_t(c)]);
			if (w.size() < 9) {
				ok = false;
				break;
			}
			bool good = true;
			auto& v = tri.corners[std::size_t(c)];
			v.position = {toNumber(w[1], &good), toNumber(w[2]), toNumber(w[3])};
			v.normal = {toNumber(w[4]), toNumber(w[5]), toNumber(w[6])};
			v.u = toNumber(w[7]);
			v.v = toNumber(w[8]);
			ok = ok && good;
		}
		i += 3;
		if (!ok) continue;
		mesh.triangles.push_back(tri);
		noteMaterial(mesh, tri.material);
	}
	if (mesh.triangles.empty()) {
		if (error) *error = "the .smd has no triangles";
		return std::nullopt;
	}
	return mesh;
}

ImportMesh placedMesh(const ImportMesh& mesh, const ModelImportOptions& options) {
	ImportMesh out = mesh;
	const double scale = options.scale > 0 && std::isfinite(options.scale) ? options.scale : 1.0;
	const auto turn = [&options](const Vec3& p) { return options.yUp ? Vec3{p.x, -p.z, p.y} : p; };
	for (auto& t : out.triangles) {
		for (auto& c : t.corners) {
			c.position = turn(c.position) * scale;
			c.normal = turn(c.normal);
		}
	}
	if (options.onGround) {
		const Bounds box = out.bounds();
		if (!box.empty()) {
			const Vec3 shift{-(box.min.x + box.max.x) / 2, -(box.min.y + box.max.y) / 2, -box.min.z};
			for (auto& t : out.triangles) {
				for (auto& c : t.corners) c.position += shift;
			}
		}
	}
	return out;
}

ImportMesh collisionMesh(const ImportMesh& placed, ModelImportOptions::Collision collision) {
	ImportMesh out;
	const std::string material = "phys";
	const auto addQuad = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
		const Vec3 n = faceNormal(a, b, c);
		out.triangles.push_back({{{{a, n}, {b, n}, {c, n}}}, material});
		out.triangles.push_back({{{{a, n}, {c, n}, {d, n}}}, material});
	};
	if (collision == ModelImportOptions::Collision::Parts) {
		out = placed;
		for (auto& t : out.triangles) t.material = material;
	} else if (collision == ModelImportOptions::Collision::Hull) {
		std::vector<Vec3> points;
		for (const auto& t : placed.triangles) {
			for (const auto& c : t.corners) points.push_back(c.position);
		}
		MapDocument doc;
		if (const auto solid = solidFromPoints(doc, points, "phys")) {
			const auto geometry = buildBrushGeometry(*solid);
			if (geometry.valid) {
				for (const auto& face : geometry.faces) {
					for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
						out.triangles.push_back({{{{face.vertices[0], face.normal}, {face.vertices[i], face.normal}, {face.vertices[i + 1], face.normal}}}, material});
					}
				}
			}
		}
	}
	if (out.triangles.empty()) {
		// Box (asked for, or the hull of a flat mesh): a little thickness so it is a solid.
		Bounds box = placed.bounds();
		if (box.empty()) return out;
		for (double* axis : {&box.max.x, &box.max.y, &box.max.z}) {
			const double* low = axis == &box.max.x ? &box.min.x : (axis == &box.max.y ? &box.min.y : &box.min.z);
			if (*axis - *low < 1) *axis = *low + 1;
		}
		const Vec3 a = box.min, b = box.max;
		const Vec3 p[8] = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z}, {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}};
		addQuad(p[0], p[3], p[2], p[1]);   // bottom (facing down)
		addQuad(p[4], p[5], p[6], p[7]);   // top
		addQuad(p[0], p[1], p[5], p[4]);
		addQuad(p[1], p[2], p[6], p[5]);
		addQuad(p[2], p[3], p[7], p[6]);
		addQuad(p[3], p[0], p[4], p[7]);
	}
	out.materials = {material};
	return out;
}

double meshVolume(const ImportMesh& mesh) {
	double volume = 0;
	for (const auto& t : mesh.triangles) {
		volume += dot(t.corners[0].position, cross(t.corners[1].position, t.corners[2].position)) / 6.0;
	}
	return std::abs(volume);
}

double importMass(const ImportMesh& placed, const ModelImportOptions& options) {
	if (options.mass > 0 && std::isfinite(options.mass)) return std::clamp(options.mass, 0.1, 50000.0);
	// About wood: 0.6 g/cm3, one cubic inch is 16.39 cm3.
	const double volume = meshVolume(collisionMesh(placed, ModelImportOptions::Collision::Hull));
	return std::clamp(std::round(volume * 16.387 * 0.6 / 1000.0), 1.0, 2000.0);
}

std::vector<std::string> modelMaterialNames(const ImportMesh& mesh) {
	std::vector<std::string> out;
	for (const auto& material : mesh.materials) {
		std::string name = importName(material);
		if (name.empty()) name = "default";
		if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
	}
	return out;
}

std::string writeSmd(const ImportMesh& mesh) {
	std::string out = "version 1\nnodes\n0 \"root\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\ntriangles\n";
	for (const auto& t : mesh.triangles) {
		std::string material = importName(t.material);
		out += (material.empty() ? std::string("default") : material) + "\n";
		for (const auto& c : t.corners) {
			out += "0 " + number(c.position.x) + " " + number(c.position.y) + " " + number(c.position.z) + " " + number(c.normal.x) + " " + number(c.normal.y) +
			       " " + number(c.normal.z) + " " + number(c.u) + " " + number(c.v) + "\n";
		}
	}
	out += "end\n";
	return out;
}

std::string modelImportName(const ModelImportOptions& options) {
	const std::string folder = importFolder(options.folder);
	return "models/" + (folder.empty() ? "" : folder + "/") + importName(options.name) + ".mdl";
}

std::string writeQc(const ModelImportOptions& options, std::string_view referenceSmd, std::string_view collisionSmd, double mass) {
	const std::string folder = importFolder(options.folder);
	const std::string model = modelImportName(options).substr(7);   // without "models/"
	std::string out = "// Made by Mapsmith (Import model). Edit and compile again with studiomdl if needed.\n";
	out += "$modelname \"" + model + "\"\n";
	out += "$body \"body\" \"" + std::string(referenceSmd) + "\"\n";
	out += "$cdmaterials \"models/" + (folder.empty() ? "" : folder + "/") + "\"\n";
	out += "$surfaceprop \"" + (options.surfaceProp.empty() ? std::string("default") : options.surfaceProp) + "\"\n";
	if (options.kind == ModelImportOptions::Kind::Static) out += "$staticprop\n";
	out += "$sequence \"idle\" \"" + std::string(referenceSmd) + "\" fps 30\n";
	out += "$collisionmodel \"" + std::string(collisionSmd) + "\"\n{\n";
	out += "\t$mass " + number(mass) + "\n";
	if (options.collision == ModelImportOptions::Collision::Parts) out += "\t$concave\n";
	out += "}\n";
	if (options.kind == ModelImportOptions::Kind::Physics) {
		out += "$keyvalues\n{\n\t\"prop_data\"\n\t{\n\t\t\"base\" \"" + std::string(mass > 200 ? "Wooden.Huge" : (mass > 30 ? "Wooden.Large" : "Wooden.Medium")) +
		       "\"\n\t}\n}\n";
	}
	return out;
}

fs::path modelSourceDir(const fs::path& modDir, const ModelImportOptions& options) {
	const std::string folder = importFolder(options.folder);
	fs::path dir = modDir / "modelsrc";
	if (!folder.empty()) dir /= fs::path(folder);
	return dir / importName(options.name);
}

std::vector<fs::path> modelOutputFiles(const fs::path& modDir, const ModelImportOptions& options) {
	const fs::path base = modDir / fs::path(modelImportName(options));
	const std::string stem = (base.parent_path() / base.stem()).string();
	return {base, stem + ".vvd", stem + ".dx90.vtx", stem + ".dx80.vtx", stem + ".sw.vtx", stem + ".phy"};
}

std::optional<ModelImportPlan> prepareModelImport(const fs::path& modDir, const ImportMesh& mesh, const ModelImportOptions& options, bool overwrite,
                                                  std::string* error) {
	const std::string name = importName(options.name);
	if (name.empty()) {
		if (error) *error = "the model needs a name";
		return std::nullopt;
	}
	if (mesh.triangles.empty()) {
		if (error) *error = "the model has no triangles";
		return std::nullopt;
	}
	const fs::path dir = modelSourceDir(modDir, options);
	const fs::path reference = dir / (name + "_ref.smd");
	const fs::path collision = dir / (name + "_phys.smd");
	const fs::path qc = dir / (name + ".qc");
	std::error_code ec;
	if (!overwrite) {
		for (const auto& file : {reference, collision, qc}) {
			if (fs::exists(file, ec)) {
				if (error) *error = displayPath(file) + " already exists";
				return std::nullopt;
			}
		}
		for (const auto& file : modelOutputFiles(modDir, options)) {
			if (fs::exists(file, ec)) {
				if (error) *error = displayPath(file) + " already exists";
				return std::nullopt;
			}
		}
	}
	const ImportMesh placed = placedMesh(mesh, options);
	const double mass = importMass(placed, options);
	ModelImportPlan plan;
	plan.record = {"model", modelImportName(options), {}, {}, {}};
	plan.qc = qc;
	const std::pair<fs::path, std::string> files[] = {
		{reference, writeSmd(placed)},
		{collision, writeSmd(collisionMesh(placed, options.collision))},
		{qc, writeQc(options, reference.filename().string(), collision.filename().string(), mass)},
	};
	for (const auto& [file, text] : files) {
		const bool existed = fs::exists(file, ec);
		if (!writeText(file, text, error)) {
			undoImport(plan.record);
			return std::nullopt;
		}
		if (!existed) plan.record.created.push_back(file);
	}
	// Compiled files from before: kept as backups, so undoing the import brings them back.
	for (const auto& file : modelOutputFiles(modDir, options)) {
		if (!fs::exists(file, ec)) continue;
		fs::copy_file(file, file.string() + ".mapsmith-backup", fs::copy_options::overwrite_existing, ec);
		if (!ec) plan.record.replaced.push_back(file);
	}
	plan.arguments = {"-game", displayPath(modDir), "-nop4", "-verbose", displayPath(qc)};
	return plan;
}

bool collectModelOutputs(ModelImportPlan& plan, const fs::path& modDir, const ModelImportOptions& options, std::string* error) {
	std::error_code ec;
	const auto outputs = modelOutputFiles(modDir, options);
	for (const auto& file : outputs) {
		if (!fs::exists(file, ec)) continue;
		const bool replaced = std::find(plan.record.replaced.begin(), plan.record.replaced.end(), file) != plan.record.replaced.end();
		const bool listed = std::find(plan.record.created.begin(), plan.record.created.end(), file) != plan.record.created.end();
		if (!replaced && !listed) plan.record.created.push_back(file);
	}
	if (!fs::exists(outputs.front(), ec)) {
		if (error) *error = "studiomdl did not write " + displayPath(outputs.front());
		return false;
	}
	return true;
}

std::vector<std::string> studiomdlProblems(std::string_view output) {
	std::vector<std::string> out;
	for (const auto line : lines(output)) {
		const std::string lower = toLower(line);
		if (lower.find("error") != std::string::npos || lower.find("warning") != std::string::npos || lower.find("too many") != std::string::npos ||
		    lower.find("can't") != std::string::npos || lower.find("cannot") != std::string::npos) {
			out.emplace_back(trim(line));
		}
	}
	return out;
}

} // namespace sme
