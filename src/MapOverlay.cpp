// Warborn: World - faction territory on Kenshi's own world map.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
// Each time Kenshi updates a town marker on its map screen, Warborn paints
// the marker in the owner faction's colour (the same colour as the journal
// map), adds the owner's name to the label, and marks towns under siege.
// The town is looked up through the marker's own handle, so a town that is
// gone is simply skipped.

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/Faction.h>
#include <kenshi/RootObjectBase.h>
#include <kenshi/util/hand.h>
#include <kenshi/gui/MapScreen.h>
#include <core/Functions.h>

static MyGUI::Colour toColour(unsigned int rgb)
{
    return MyGUI::Colour((float)((rgb >> 16) & 255) / 255.0f, (float)((rgb >> 8) & 255) / 255.0f, (float)(rgb & 255) / 255.0f, 1.0f);
}

static void (*s_markerOrig)(MapScreen::MapMarkerTown*, const MyGUI::IntPoint&, MapZoomLevel) = NULL;
static void marker_hook(MapScreen::MapMarkerTown* self, const MyGUI::IntPoint& pos, MapZoomLevel zoom)
{
    s_markerOrig(self, pos, zoom);
    if (!self) return;
    RootObjectBase* town = self->handle.getRootObjectBase();     // NULL if the town is gone
    if (!town) return;
    Faction* owner = town->getFaction();
    if (!owner) return;
    MyGUI::Colour c = toColour(wbFactionColour(owner));
    if (self->image) self->image->setColour(c);
    if (self->label)
    {
        bool siege = wbTownBesieged(town);
        std::string text = town->getName() + "  (" + wbFactionName(owner) + ")";
        if (siege) text = "#FF4040[SIEGE] #FFFFFF" + text;
        self->label->setCaption(text);
        self->label->setTextColour(siege ? MyGUI::Colour::White : c);
    }
}

void wbMapOverlayInstall()
{
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(&MapScreen::MapMarkerTown::update),
                                                 (void*)&marker_hook, &s_markerOrig))
        ErrorLog("Warborn: map marker hook FAILED - no territory colours on the world map");
    else
        DebugLog("Warborn: map marker hook OK (territory on the world map)");
}
