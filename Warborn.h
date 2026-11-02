// Warborn: KCD 2-style combat layer for Kenshi.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// Phase 1: skill/armor-based damage + hit impact (hit-stop, camera shake).
// The host plugin (WASDCombatPlugin.cpp) calls these from its existing hooks.
// The module never reads globals: everything it needs is passed in, so it
// stays independent of which KenshiLib header version declares what.
#pragma once

#include <kenshi/Enums.h>

class Character;
class Damages;
class GameWorld;
namespace Ogre { class Camera; }

// Read the [Warborn] section of the plugin's .ini (call once at startup).
void warbornLoadConfig(const char* iniPath);

// Melee hit, BEFORE the game resolves it.  victim = the one being hit,
// attacker = the one swinging, player = the character the player controls
// right now (may be null).  May scale `damage`.
void warbornBeforeHit(Character* victim, Character* attacker, CutDirection dir,
                      Damages& damage, Character* player);

// Same hit, AFTER the game resolved it (result = flesh / armour / blocked...).
// Queues hit-stop and camera shake when the player is involved.
void warbornAfterHit(Character* victim, Character* attacker, HitMaterialType result,
                     Character* player);

// Once per frame from the main-loop hook (main thread): runs hit-stop timing.
void warbornFrame(GameWorld* world);

// Player camera, after the game (and Direct Control) placed it this frame:
// applies the camera shake.  Pass ONLY the player's own camera.
void warbornCamera(Ogre::Camera* cam);

// Saves are loading / plugin state is being reset: drop pending effects.
void warbornReset(GameWorld* world);
