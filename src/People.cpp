// Warborn: World - reputation, recruiting, squad commands.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// Reputation (original mechanic, "the enemy of my enemy"):
//   Every enemy fighter your squad fells earns a little respect from every
//   faction you know that is at war with theirs (capped, so it can't be
//   farmed). Fighting in a war at a town, and contracts, earn much more.
//
// Recruiting: click a person to select them, press RecruitKey (Y). They may
//   join you: your reputation with their faction decides it. Wanderers from
//   small clans join more easily, people of great powers are more loyal.
//
// Commands (you give orders, you don't take control):
//   Alt+1 follow me, Alt+2 hold position, Alt+3 charge (each picks the nearest enemy),
//   Alt+4 focus (everyone attacks the enemy nearest the leader). The leader is
//   your selected character; orders go to your people near them.

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <kenshi/RootObject.h>
#include <ogre/OgreMemoryAllocatorConfig.h>
#include <ogre/OgreVector3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <algorithm>

extern bool g_wbRecruitPressed;
extern bool g_wbRecruitShift;
extern bool g_wbCmdPressed[4];

// ------------------------------------------------------------ renown
// Your personal fame. Deeds raise it; titles mark the way; it makes people
// more willing to follow you.
static float s_renown = 0.0f;
static int   s_titleIdx = 0;
struct Title { float at; const char* name; };
static const Title TITLES[] = {
    { 0.0f, "Nobody" }, { 8.0f, "Known Drifter" }, { 20.0f, "Sellsword" }, { 40.0f, "Blade for Hire" },
    { 70.0f, "Captain" }, { 110.0f, "Warlord" }, { 170.0f, "Champion of the Wastes" }, { 250.0f, "Living Legend" },
};
static const int TITLE_COUNT = (int)(sizeof(TITLES) / sizeof(TITLES[0]));
float wbRenown() { return s_renown; }
const char* wbRenownTitle() { return TITLES[s_titleIdx].name; }
void wbAddRenown(float amount, const char* why)
{
    if (amount <= 0.0f) return;
    s_renown += amount;
    wbFollowersGlory(amount);                         // your people share your glory
    char lb[160]; sprintf_s(lb, sizeof(lb), "[Warborn] renown +%.1f (%s) = %.1f", amount, why ? why : "", s_renown); wbLog(lb);
    int idx = s_titleIdx;
    while (idx + 1 < TITLE_COUNT && s_renown >= TITLES[idx + 1].at) ++idx;
    if (idx != s_titleIdx)
    {
        s_titleIdx = idx;
        WbVars v; v.X = TITLES[idx].name;
        wbNews(wbLine(LINE_NEW_TITLE, v).c_str());
    }
}

// ------------------------------------------------------------ reputation
struct RepBucket { Faction* f; float given; double windowStart; float pendingToast; };
static std::vector<RepBucket> s_rep;

static RepBucket& bucket(Faction* f, double now)
{
    for (size_t i = 0; i < s_rep.size(); ++i)
        if (s_rep[i].f == f)
        {
            if (now - s_rep[i].windowStart > 10.0) { s_rep[i].windowStart = now; s_rep[i].given = 0.0f; }
            return s_rep[i];
        }
    RepBucket b; b.f = f; b.given = 0.0f; b.windowStart = now; b.pendingToast = 0.0f;
    s_rep.push_back(b);
    return s_rep.back();
}

void wbGiveRep(Faction* f, float amount, const char* why)
{
    Faction* pf = wbPlayerFaction();
    if (!f || !pf || f == pf || !f->relations || amount == 0.0f) return;
    if (amount > 0.0f) amount *= wbCourtRepMult();       // your Envoy speaks for you
    float before = f->relations->getFactionRelation(pf);
    f->relations->affectRelations(pf, amount, 1.0f);
    float after = f->relations->getFactionRelation(pf);
    char lb[220];
    sprintf_s(lb, sizeof(lb), "[Warborn] rep %s %+.1f (%s) theirView %.1f->%.1f ourView %.1f",
              wbFactionName(f).c_str(), amount, why, before, after,
              pf->relations ? pf->relations->getFactionRelation(f) : 0.0f);
    wbLog(lb);
    if (fabsf(amount) >= 1.0f)
    {
        char b[200];
        sprintf_s(b, sizeof(b), "Reputation %+d with the %s %s.", (int)(amount + (amount > 0 ? 0.5f : -0.5f)),
                  wbFactionName(f).c_str(), why);
        wbToast(b);
    }
}

// Small, capped rep from every felled enemy - toasted once a whole point builds up.
static void smallRep(Faction* f, float amount, double now)
{
    RepBucket& b = bucket(f, now);
    float room = g_wb.repCapPerFaction - b.given;
    if (room <= 0.0f) return;
    if (amount > room) amount = room;
    b.given += amount;
    Faction* pf = wbPlayerFaction();
    if (f->relations && pf) f->relations->affectRelations(pf, amount, 1.0f);
    b.pendingToast += amount;
    if (b.pendingToast >= 1.0f)
    {
        char t[200];
        sprintf_s(t, sizeof(t), "The %s approve: you fight their enemies (+%d).",
                  wbFactionName(f).c_str(), (int)b.pendingToast);
        wbToast(t);
        b.pendingToast -= (float)(int)b.pendingToast;
    }
}

void wbPeopleOnFelled(Faction* vf, float x, float z)
{
    if (!vf || wbIsPlayerFaction(vf)) return;
    wbWorldOnFelled(vf, x, z);
    wbContractsOnFelled(vf);

    wbAddRenown(0.25f, "an enemy felled");

    // the enemy of my enemy...
    if (!ou || !ou->factionMgr || !ou->player) return;
    const lektor<Faction*>* all = ou->factionMgr->getAllFactions();
    if (!all) return;
    double now = wbWorldMinutes();
    for (uint32_t i = 0; i < all->size(); ++i)
    {
        Faction* f = (*all)[i];
        if (!f || f == vf || wbIsPlayerFaction(f) || !f->relations) continue;
        if (!ou->player->isFactionKnown(f)) continue;
        if (!f->relations->isEnemy(vf)) continue;
        smallRep(f, g_wb.repPerFelled, now);
    }
}

struct RepRow { float rel; std::string name; };
static bool repLess(const RepRow& a, const RepRow& b) { return a.rel > b.rel; }
std::string wbReputationSummary()
{
    Faction* pf = wbPlayerFaction();
    if (!pf || !ou || !ou->factionMgr || !ou->player) return std::string("  (no information yet)\n");
    const lektor<Faction*>* all = ou->factionMgr->getAllFactions();
    std::vector<RepRow> rows;
    for (uint32_t i = 0; all && i < all->size(); ++i)
    {
        Faction* f = (*all)[i];
        if (!f || f == pf || !f->relations || !ou->player->isFactionKnown(f)) continue;
        RepRow r; r.rel = f->relations->getFactionRelation(pf); r.name = wbFactionName(f);
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), repLess);
    std::string out; char b[160];
    for (size_t i = 0; i < rows.size() && i < 16; ++i)
    {
        const char* word = rows[i].rel >= 50 ? "allied" : rows[i].rel >= 15 ? "friendly"
                         : rows[i].rel > -15 ? "neutral" : rows[i].rel > -50 ? "unfriendly" : "hostile";
        sprintf_s(b, sizeof(b), "  %+4d  %s (%s)\n", (int)rows[i].rel, rows[i].name.c_str(), word);
        out += b;
    }
    if (out.empty()) out = "  You are unknown. Meet people and make a name.\n";
    char t[160];
    sprintf_s(t, sizeof(t), "\nRENOWN: %d - known as \"%s\"\n", (int)s_renown, TITLES[s_titleIdx].name);
    out += t;
    if (s_titleIdx + 1 < TITLE_COUNT)
    {
        sprintf_s(t, sizeof(t), "  Next title at %d: \"%s\"\n", (int)TITLES[s_titleIdx + 1].at, TITLES[s_titleIdx + 1].name);
        out += t;
    }
    return out;
}

// ------------------------------------------------------------ recruiting
struct Refusal { Character* c; double until; };
static std::vector<Refusal> s_refusals;

static Character* firstConsciousPlayerChar()
{
    if (!ou || !ou->player) return NULL;
    const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
    for (uint32_t i = 0; i < mine.size(); ++i)
    {
        Character* c = mine[i];
        if (c && !c->isDead() && !c->isUnconcious()) return c;
    }
    return NULL;
}

Character* wbSelectedPerson()
{
    if (!ou || !ou->player) return NULL;
    Character* pointed = wbPersonUnderMouse(70.0f);     // whoever the cursor is on
    if (pointed) return pointed;
    lektor<RootObject*> sel;
    ou->player->getAllSelectedObjects(sel, CHARACTER);
    Character* stranger = NULL;
    for (uint32_t i = 0; i < sel.size(); ++i)
    {
        Character* c = static_cast<Character*>(sel[i]);
        if (c && !c->isPlayerCharacter()) { stranger = c; break; }
    }
    if (sel.stuff) Ogre::CategorisedAllocPolicy<Ogre::MEMCATEGORY_GENERAL>::deallocateBytes(sel.stuff);
    char lb[96]; sprintf_s(lb, sizeof(lb), "[Warborn] selected: %u objects, stranger=%d", sel.size(), stranger ? 1 : 0); wbLog(lb);
    if (stranger) return stranger;
    return ou->player->selectedCharacter.getCharacter();
}

// Kenshi's own recruit (the same as recruiting through dialogue); our
// fallback only if the game says no.
static bool joinBand(Character* c, Character* leader)
{
    bool ok = ou->player->recruit(c, false);
    if (!ok || !c->isPlayerCharacter()) { c->setFaction(wbPlayerFaction(), leader->getPlatoon()); ok = c->isPlayerCharacter(); }
    char lb[96]; sprintf_s(lb, sizeof(lb), "[Warborn] join band ok=%d", ok ? 1 : 0); wbLogAlways(lb);
    return ok;
}

// For the court's General: recruit someone directly (no chance roll here).
bool wbJoinBand(Character* c)
{
    Character* leader = firstConsciousPlayerChar();
    if (!c || !leader) return false;
    return joinBand(c, leader);
}

static void tryRecruit()
{
    Character* c = wbSelectedPerson();
    if (!c) { wbToast("Point at a person with the mouse, then press Alt+R."); return; }
    if (c->isPlayerCharacter()) { wbToast("They already ride with you."); return; }
    if (c->isDead()) { wbToast("The dead don't follow anyone."); return; }
    Faction* pf = wbPlayerFaction();
    Character* leader = firstConsciousPlayerChar();
    if (!pf || !leader) return;
    // PRISONER: someone knocked out and helpless is taken to work for you
    if (c->isUnconcious() || c->isDown())
    {
        Faction* of = c->getFaction();
        std::string pn = c->getName();
        joinBand(c, leader);
        wbFollowerJoined(c, of, true);
        if (of && !wbIsPlayerFaction(of)) wbGiveRep(of, -5.0f, "for taking one of theirs prisoner");
        wbNews((pn + " has been taken prisoner and put to work. They'll try to escape until they come to trust you.").c_str());
        return;
    }

    double now = wbWorldMinutes();
    for (size_t i = 0; i < s_refusals.size(); ++i)
        if (s_refusals[i].c == c && now < s_refusals[i].until) { wbToast("They already gave you their answer. Ask again later."); return; }

    std::string name = c->getName();
    Faction* vf = c->getFaction();
    WbVars v; v.N = name; v.F = wbFactionName(vf);
    if (vf && vf->relations && vf->relations->isEnemy(pf))
    {
        wbToast(wbLine(LINE_RECRUIT_HOSTILE, v).c_str()); return;
    }
    float rel = (vf && vf->relations) ? vf->relations->getFactionRelation(pf) : 0.0f;
    int towns = wbTownCount(vf);
    float chance = g_wb.recruitBase + 0.6f * rel / 100.0f;
    if (towns == 0) chance += 0.15f;                  // drifters and wanderers
    if (towns >= 5) chance *= 0.6f;                   // loyal to a great power
    float fame = s_renown / 400.0f; if (fame > 0.25f) fame = 0.25f;
    chance += fame;                                   // your name carries weight
    chance += wbRecruitBonus();                       // exiles attract outcasts
    if (chance < 0.02f) chance = 0.02f;
    if (chance > 0.95f) chance = 0.95f;

    bool yes = wbRand01() < chance;
    char lb[200];
    sprintf_s(lb, sizeof(lb), "[Warborn] recruit %s faction=%s rel=%.0f chance=%.2f -> %d",
              name.c_str(), wbFactionName(vf).c_str(), rel, chance, yes ? 1 : 0);
    wbLogAlways(lb);
    if (!yes)
    {
        Refusal r; r.c = c; r.until = now + 10.0; s_refusals.push_back(r);
        char b[300]; sprintf_s(b, sizeof(b), "%s  (chance %d%%)", wbLine(LINE_RECRUIT_NO, v).c_str(), (int)(chance * 100.0f));
        wbToast(b); return;
    }
    joinBand(c, leader);
    wbFollowerJoined(c, vf, false);
    if (g_wbRecruitShift) { wbWarbandAdd(c); wbNews((name + " joins your warband - yours to command, not to control. " + wbLine(LINE_RECRUIT_YES, v)).c_str()); }
    else wbNews((name + " has joined your band! " + wbLine(LINE_RECRUIT_YES, v)).c_str());
    wbAddRenown(1.0f, "a new follower");
}

// ------------------------------------------------------------ commands
static const char* const CMD_NAME[4] = { "FOLLOW ME", "HOLD POSITION", "CHARGE!", "FOCUS THEIR LEADER'S FOE" };

static Character* nearestEnemyOf(Character* c, float range)
{
    if (!c || !ou) return NULL;
    Ogre::Vector3 p = c->getPosition();
    Character* best = NULL; float bestD2 = range * range;
    auto& chars = ou->getCharacterUpdateList();
    for (auto it = chars.begin(); it != chars.end(); ++it)
    {
        Character* e = *it;
        if (!e || e == c) continue;
        Ogre::Vector3 q = e->getPosition();
        float dx = q.x - p.x, dz = q.z - p.z, d2 = dx * dx + dz * dz;
        if (d2 >= bestD2) continue;
        if (e->isDead() || e->isUnconcious() || e->isDown() || !e->isEnemy(c, true)) continue;
        bestD2 = d2; best = e;
    }
    return best;
}

static void giveCommand(int cmd)
{
    Character* leader = ou->player->selectedCharacter.getCharacter();
    if (!leader || !leader->isPlayerCharacter()) leader = firstConsciousPlayerChar();
    if (!leader) return;
    Ogre::Vector3 lp = leader->getPosition();
    Character* focus = NULL;
    if (cmd == 3)
    {
        focus = nearestEnemyOf(leader, g_wb.cmdRange * 3.0f);
        if (!focus) { wbToast("No enemy near your leader."); return; }
    }
    int n = 0;
    const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
    for (uint32_t i = 0; i < mine.size(); ++i)
    {
        Character* c = mine[i];
        if (!c || c->isDead() || c->isUnconcious() || c->isDown()) continue;
        if (cmd == 0 && c == leader) continue;
        Ogre::Vector3 cp = c->getPosition();
        float dx = cp.x - lp.x, dz = cp.z - lp.z;
        if (dx * dx + dz * dz > g_wb.cmdRange * g_wb.cmdRange) continue;
        if (!c->canTakePlayerOrdersAtThisTime()) continue;
        switch (cmd)
        {
            case 0: c->addOrder(NULL, FOLLOW_PLAYER_ORDER, (RootObject*)leader, false, true, lp); break;
            case 1: c->addOrder(NULL, HOLD_POSITION, NULL, false, true, cp); break;
            case 2:
            {
                Character* foe = nearestEnemyOf(c, g_wb.cmdRange * 3.0f);
                if (!foe) continue;
                c->addOrder(NULL, MELEE_ATTACK, (RootObject*)foe, false, true, foe->getPosition());
                break;
            }
            case 3: c->addOrder(NULL, MELEE_ATTACK, (RootObject*)focus, false, true, focus->getPosition()); break;
        }
        ++n;
    }
    char b[120];
    if (n > 0) sprintf_s(b, sizeof(b), "Your people (%d): %s", n, CMD_NAME[cmd]);
    else       sprintf_s(b, sizeof(b), "%s - nobody near your leader can hear you.", CMD_NAME[cmd]);
    wbToast(b);
    char lb[96]; sprintf_s(lb, sizeof(lb), "[Warborn] command %d given=%d", cmd, n);
    wbLog(lb);
}

// ------------------------------------------------------------ frame
void wbPeopleFrame()
{
    if (!ou || !ou->player) return;
    if (g_wbRecruitPressed) tryRecruit();
    for (int i = 0; i < 4; ++i) if (g_wbCmdPressed[i]) giveCommand(i);
}

void wbPeopleSave(std::vector<std::string>& out)
{
    char b[64]; sprintf_s(b, sizeof(b), "renown\t%.2f", s_renown);
    out.push_back(b);
}
void wbPeopleLoad(const std::vector<std::string>& in)
{
    for (size_t i = 0; i < in.size(); ++i)
    {
        std::vector<std::string> f = wbSplit(in[i]);
        if (f[0] == "renown" && f.size() >= 2)
        {
            s_renown = (float)atof(f[1].c_str());
            s_titleIdx = 0;
            while (s_titleIdx + 1 < TITLE_COUNT && s_renown >= TITLES[s_titleIdx + 1].at) ++s_titleIdx;   // quietly
        }
    }
}

void wbPeopleReset()
{
    s_renown = 0.0f; s_titleIdx = 0;
    s_rep.clear();
    s_refusals.clear();
}
