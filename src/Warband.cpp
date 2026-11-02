// Warborn: World - your warband: people you COMMAND with missions, not control.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
//   B  add / remove the selected member of your squad to / from the warband
//   U  "Take that place": the warband marches on the nearest enemy town or
//      camp, fights what it finds and, once it is cleared and held for a
//      while, the place is YOURS (PlayerConquest).
//   I  patrol a ring around your selected character, fighting any enemy met
//   O  return: the warband follows your selected character again
// Missions are carried out with Kenshi's own orders (move, attack, follow).

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/FactionWarMgr.h>
#include <kenshi/Town.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <string>

extern bool g_wbWarbandKeys[4];        // B, U, I, O (Main.cpp)

enum { MISSION_NONE = 0, MISSION_TAKE, MISSION_PATROL };
static std::vector<Character*> s_band;
static std::vector<std::string> s_bandNamesToRestore;
static int        s_mission      = MISSION_NONE;
static TownBase*  s_targetTown   = NULL;
static Faction*   s_targetOwner  = NULL;
static std::string s_targetName;
static Ogre::Vector3 s_patrolCentre(0, 0, 0);
static int        s_patrolLeg    = 0;
static double     s_nextThink    = 0.0;
static double     s_clearSince   = -1.0;
static double     s_missionStart = 0.0;

static bool alive(Character* c) { return c && !c->isDead() && !c->isUnconcious(); }
static float dist2(const Ogre::Vector3& a, const Ogre::Vector3& b) { float dx = a.x - b.x, dz = a.z - b.z; return dx * dx + dz * dz; }

// band members must still be ours (dead / sold / lost ones drop out)
static void pruneBand()
{
    if (!ou || !ou->player) { s_band.clear(); return; }
    const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
    for (size_t i = 0; i < s_band.size(); )
    {
        bool still = false;
        for (uint32_t k = 0; k < mine.size(); ++k) if (mine[k] == s_band[i]) { still = true; break; }
        if (still && !s_band[i]->isDead()) ++i;
        else s_band.erase(s_band.begin() + i);
    }
}
static Character* selectedOwn()
{
    Character* c = ou->player->selectedCharacter.getCharacter();
    return (c && c->isPlayerCharacter()) ? c : NULL;
}
static Character* nearestEnemy(Character* c, float range)
{
    Ogre::Vector3 p = c->getPosition();
    Character* best = NULL; float bestD2 = range * range;
    auto& chars = ou->getCharacterUpdateList();
    for (auto it = chars.begin(); it != chars.end(); ++it)
    {
        Character* e = *it;
        if (!e || e == c || !alive(e) || e->isDown() || !e->isEnemy(c, true)) continue;
        float d2 = dist2(e->getPosition(), p);
        if (d2 < bestD2) { bestD2 = d2; best = e; }
    }
    return best;
}
static int enemiesNear(const Ogre::Vector3& at, float range, Character* ref)
{
    int n = 0;
    auto& chars = ou->getCharacterUpdateList();
    for (auto it = chars.begin(); it != chars.end(); ++it)
    {
        Character* e = *it;
        if (!e || !alive(e) || e->isDown() || !e->isEnemy(ref, true)) continue;
        if (dist2(e->getPosition(), at) <= range * range) ++n;
    }
    return n;
}

void wbWarbandAdd(Character* c)
{
    if (!c) return;
    for (size_t i = 0; i < s_band.size(); ++i) if (s_band[i] == c) return;
    s_band.push_back(c);
}

static void toggleMember()
{
    Character* c = selectedOwn();
    if (!c) { wbToast("Select one of your people, then press the warband key."); return; }
    std::string name = c->getName();
    for (size_t i = 0; i < s_band.size(); ++i)
        if (s_band[i] == c)
        {
            s_band.erase(s_band.begin() + i);
            wbToast((name + " leaves the warband and returns to your side.").c_str());
            return;
        }
    s_band.push_back(c);
    char b[200]; sprintf_s(b, sizeof(b), "%s joins the warband (%d strong).", name.c_str(), (int)s_band.size());
    wbToast(b);
}

static TownBase* nearestEnemyTown(const Ogre::Vector3& from, Faction** ownerOut)
{
    Faction* pf = wbPlayerFaction();
    if (!pf || !ou->factionMgr) return NULL;
    const lektor<Faction*>* all = ou->factionMgr->getAllFactions();
    TownBase* best = NULL; float bestD2 = 1e30f;
    for (uint32_t i = 0; all && i < all->size(); ++i)
    {
        Faction* f = (*all)[i];
        if (!f || f == pf || !f->warMgr || !f->relations || !f->relations->isEnemy(pf)) continue;
        lektor<TownBase*>& towns = f->warMgr->myTowns;
        for (uint32_t k = 0; k < towns.size(); ++k)
        {
            if (!towns[k]) continue;
            float d2 = dist2(towns[k]->getPosition(), from);
            if (d2 < bestD2) { bestD2 = d2; best = towns[k]; *ownerOut = f; }
        }
    }
    return best;
}

static void orderMove(const Ogre::Vector3& to)
{
    for (size_t i = 0; i < s_band.size(); ++i)
        if (alive(s_band[i]) && s_band[i]->canTakePlayerOrdersAtThisTime())
            s_band[i]->playerMoveOrderDefault(NULL, NULL, to);
}
static int orderFight(float range)
{
    int n = 0;
    for (size_t i = 0; i < s_band.size(); ++i)
    {
        Character* c = s_band[i];
        if (!alive(c) || c->isDown() || !c->canTakePlayerOrdersAtThisTime()) continue;
        Character* foe = nearestEnemy(c, range);
        if (!foe) continue;
        c->addOrder(NULL, MELEE_ATTACK, (RootObject*)foe, false, true, foe->getPosition());
        ++n;
    }
    return n;
}

static void startTake()
{
    Character* lead = selectedOwn();
    if (s_band.empty()) { wbToast("Your warband is empty. Select people and press B to add them."); return; }
    Ogre::Vector3 from = lead ? lead->getPosition() : s_band[0]->getPosition();
    Faction* owner = NULL;
    TownBase* t = nearestEnemyTown(from, &owner);
    if (!t) { wbToast("No enemy town or camp is known nearby."); return; }
    s_mission = MISSION_TAKE; s_targetTown = t; s_targetOwner = owner; s_targetName = wbTownName(t);
    s_clearSince = -1.0; s_missionStart = wbWorldMinutes(); s_nextThink = 0.0;
    orderMove(t->getPosition());
    char b[260];
    sprintf_s(b, sizeof(b), "Your warband (%d) marches on %s of the %s.", (int)s_band.size(),
              s_targetName.c_str(), wbFactionName(owner).c_str());
    wbNews(b);
}
static void startPatrol()
{
    Character* lead = selectedOwn();
    if (s_band.empty()) { wbToast("Your warband is empty. Select people and press B to add them."); return; }
    s_mission = MISSION_PATROL;
    s_patrolCentre = lead ? lead->getPosition() : s_band[0]->getPosition();
    s_patrolLeg = 0; s_nextThink = 0.0;
    wbToast("Your warband begins to patrol the area.");
}
static void recall()
{
    Character* lead = selectedOwn();
    s_mission = MISSION_NONE; s_targetTown = NULL;
    if (!lead) { wbToast("Select the one they should follow."); return; }
    for (size_t i = 0; i < s_band.size(); ++i)
        if (alive(s_band[i]) && s_band[i] != lead && s_band[i]->canTakePlayerOrdersAtThisTime())
            s_band[i]->addOrder(NULL, FOLLOW_PLAYER_ORDER, (RootObject*)lead, false, true, lead->getPosition());
    wbToast("Your warband returns to you.");
}

static Ogre::Vector3 bandCentre(int* aliveOut)
{
    Ogre::Vector3 c(0, 0, 0); int n = 0;
    for (size_t i = 0; i < s_band.size(); ++i) if (alive(s_band[i])) { c += s_band[i]->getPosition(); ++n; }
    if (n) c /= (float)n;
    *aliveOut = n;
    return c;
}

static void thinkTake(double now)
{
    Faction* owner = NULL;
    // the place may have changed hands meanwhile (someone else conquered it?)
    bool exists = false;
    if (s_targetOwner && s_targetOwner->warMgr)
    {
        lektor<TownBase*>& towns = s_targetOwner->warMgr->myTowns;
        for (uint32_t k = 0; k < towns.size(); ++k) if (towns[k] == s_targetTown) { exists = true; owner = s_targetOwner; break; }
    }
    if (!exists) { wbNews((s_targetName + " is no longer held by your foes. The warband stands down.").c_str()); s_mission = MISSION_NONE; return; }

    int n = 0;
    Ogre::Vector3 c = bandCentre(&n);
    if (n == 0) { wbNews(("Your warband has been destroyed at " + s_targetName + ".").c_str()); s_mission = MISSION_NONE; return; }
    Ogre::Vector3 t = s_targetTown->getPosition();
    bool arrived = dist2(c, t) <= 900.0f * 900.0f;           // ~90 m
    if (!arrived)
    {
        if (now - s_missionStart > 0.5) orderMove(t);          // keep them marching
        orderFight(250.0f);                                    // and let them fight what blocks the road
        return;
    }
    int fighting = orderFight(800.0f);
    int foes = enemiesNear(t, 1200.0f, s_band[0]);
    if (foes > 0 || fighting > 0) { s_clearSince = -1.0; return; }
    if (s_clearSince < 0.0) { s_clearSince = now; wbToast(("The defenders of " + s_targetName + " are beaten. Hold the place...").c_str()); return; }
    if (now - s_clearSince < 0.5) return;                      // hold it ~30 s
    if (g_wb.playerConquest && wbPlayerFaction())
    {
        s_targetTown->setFaction(wbPlayerFaction(), NULL);
        wbNews((s_targetName + " has been taken by your warband! It flies your banner now.").c_str());
        wbGiveRep(s_targetOwner, -20.0f, "for taking their town");
        wbAddRenown(10.0f, "a town conquered");
    }
    else wbNews(("Your warband has cleared " + s_targetName + ".").c_str());
    s_mission = MISSION_NONE;
}

static void thinkPatrol()
{
    int n = 0;
    Ogre::Vector3 c = bandCentre(&n);
    if (n == 0) { s_mission = MISSION_NONE; return; }
    if (orderFight(400.0f) > 0) return;                       // enemies first
    static const float R = 500.0f;                            // ~50 m ring, four legs
    float a = 1.5707963f * (float)s_patrolLeg;
    Ogre::Vector3 wp(s_patrolCentre.x + R * cosf(a), s_patrolCentre.y, s_patrolCentre.z + R * sinf(a));
    if (dist2(c, wp) < 120.0f * 120.0f) { s_patrolLeg = (s_patrolLeg + 1) % 4; a = 1.5707963f * (float)s_patrolLeg;
        wp = Ogre::Vector3(s_patrolCentre.x + R * cosf(a), s_patrolCentre.y, s_patrolCentre.z + R * sinf(a)); }
    orderMove(wp);
}

void wbWarbandFrame()
{
    if (!ou || !ou->player) return;
    if (!s_bandNamesToRestore.empty())
    {
        const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
        for (size_t i = 0; i < s_bandNamesToRestore.size(); ++i)
            for (uint32_t k = 0; k < mine.size(); ++k)
                if (mine[k] && mine[k]->getName() == s_bandNamesToRestore[i]) { s_band.push_back(mine[k]); break; }
        s_bandNamesToRestore.clear();
    }
    if (g_wbWarbandKeys[0]) toggleMember();
    if (g_wbWarbandKeys[1]) startTake();
    if (g_wbWarbandKeys[2]) startPatrol();
    if (g_wbWarbandKeys[3]) recall();
    double now = wbWorldMinutes();
    if (s_mission == MISSION_NONE || now < s_nextThink) return;
    s_nextThink = now + 0.05;                                 // every ~3 s
    pruneBand();
    if (s_band.empty()) { s_mission = MISSION_NONE; return; }
    if (s_mission == MISSION_TAKE) thinkTake(now);
    else if (s_mission == MISSION_PATROL) thinkPatrol();
}

std::string wbWarbandSummary()
{
    std::string out;
    char b[200];
    sprintf_s(b, sizeof(b), "YOUR WARBAND (%d): ", (int)s_band.size());
    out += b;
    for (size_t i = 0; i < s_band.size(); ++i) { if (i) out += ", "; out += s_band[i]->getName(); }
    out += "\n";
    if (s_mission == MISSION_TAKE)        out += "  Mission: take " + s_targetName + "\n";
    else if (s_mission == MISSION_PATROL) out += "  Mission: patrolling\n";
    else                                  out += "  Mission: none (B add/remove, U take a place, I patrol, O return)\n";
    return out;
}

void wbWarbandSave(std::vector<std::string>& out)
{
    for (size_t i = 0; i < s_band.size(); ++i) out.push_back("band\t" + wbClean(s_band[i]->getName()));
}
void wbWarbandLoad(const std::vector<std::string>& in)
{
    for (size_t i = 0; i < in.size(); ++i)
        if (in[i].compare(0, 5, "band\t") == 0) s_bandNamesToRestore.push_back(in[i].substr(5));
}
void wbWarbandReset()
{
    s_band.clear(); s_bandNamesToRestore.clear();
    s_mission = MISSION_NONE; s_targetTown = NULL; s_targetOwner = NULL;
}
