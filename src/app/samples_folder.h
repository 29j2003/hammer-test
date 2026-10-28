// samples_folder.h - Where Mapsmith's sample files are (sme/samples.h): the "Use a sample ..."
// buttons of the import dialogs and the Animations page open it.

#pragma once

#include <QString>

namespace sme::app {

/// The samples folder: $SME_SAMPLES_DIR, else "samples" next to the program (the installed
/// Mapsmith), else in a folder above it (a build: <build>/samples). Empty if there is none.
[[nodiscard]] QString samplesFolder();
/// A part of it ("sounds", "animations", ...), or the folder itself if that part is missing;
/// empty without samples.
[[nodiscard]] QString samplesFolder(const char* part);

} // namespace sme::app
