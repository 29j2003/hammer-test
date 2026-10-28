// asset_menu.h - The "Show in ..." entries for a game asset (material, model, sound, particle
// system, soundscape). Panels that name an asset ask the main window for these entries through an
// AssetMenuFiller, so every menu offers the same choices and opens them the same way.

#pragma once

#include <functional>

#include <sme/asset_refs.h>

class QMenu;

namespace sme::app {

/// Where the menu opens; the entry that would lead back to the same place is left out.
enum class AssetPlace { View, Properties, AssetBrowser, MaterialsPage, ModelsPage };

/// Adds the "Show in ..." / "Find in map" entries for `asset` to `menu`.
using AssetMenuFiller = std::function<void(QMenu* menu, const AssetRef& asset, AssetPlace from)>;

} // namespace sme::app
