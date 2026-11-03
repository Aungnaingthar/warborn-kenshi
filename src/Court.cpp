// Warborn: World - your court: officers who run your nation for you.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// In the journal's Court tab you appoint one of your people (the selected
// one) to an office. An officer leaves your field squad for a separate Court
// squad, stays put, and does their job every council (10 minutes):
//
//   FINANCE MINISTER  trade income, better tax collection, shop rents.
//                     Science and Engineering make a good minister. A
//                     disloyal one quietly embezzles.
//   STEWARD           develops your towns (levels 0-10): merchants move in
//                     and open shops, farms feed your people. Keeps the food
//                     stores; when food runs out, your people go hungry and
//                     their bond with you suffers. Engineering, Labouring and
//                     Farming make a good steward.
//   GENERAL           recruits soldiers on their own from the people around
//                     them, and leads the army: you set how many soldiers go,
//                     then order the march on the nearest enemy town.
//                     Melee skill and your renown help recruiting.
//
// Development and shops are an economy simulation: Kenshi has no safe way for
// a mod to place buildings, so no new buildings appear in the world.

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/FactionWarMgr.h>
#include <kenshi/Enums.h>
#include <ogre/OgreVector3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <algorithm>

enum { OFF_FINANCE = 0, OFF_STEWARD, OFF_GENERAL, OFF_COUNT };
static const char* const OFFICE_NAME[OFF_COUNT] = { "Finance Minister", "Steward", "General" };

static Character*   s_off[OFF_COUNT]     = { NULL, NULL, NULL };
static std::string  s_offRestore[OFF_COUNT];
static ActivePlatoon* s_courtSquad      = NULL;
static float        s_food              = 60.0f;
static int          s_armySize          = 4;
static double       s_nextCouncil       = 10.0;
static double       s_nextRecruit       = 5.0;
struct TownDev { std::string town; int level; int shops; };
static std::vector<TownDev> s_dev;
static std::string  s_lastReport;

// ------------------------------------------------------------ helpers
static float stat(Character* c, StatsEnumerated s)
{
    CharStats* st = c ? c->getStats() : NULL;
    return st ? st->getStat(s, false) : 0.0f;
}
static float financeSkill(Character* c) { return (stat(c, STAT_SCIENCE) + 0.5f * stat(c, STAT_ENGINEERING)) / 1.5f; }
static float stewardSkill(Character* c) { return (stat(c, STAT_ENGINEERING) + stat(c, STAT_LABOURING) + stat(c, STAT_FARMING)) / 3.0f; }
static float generalSkill(Character* c) { return (stat(c, STAT_MELEE_ATTACK) + stat(c, STAT_MELEE_DEFENCE)) / 2.0f; }
static bool officerOk(int o) { return s_off[o] && wbIsOwned(s_off[o]) && !s_off[o]->isDead(); }
bool wbIsOfficer(Character* c)
{
    for (int o = 0; o < OFF_COUNT; ++o) if (c && s_off[o] == c) return true;
    return false;
}
static TownDev& devOf(const std::string& town)
{
    for (size_t i = 0; i < s_dev.size(); ++i) if (s_dev[i].town == town) return s_dev[i];
    TownDev d; d.town = town; d.level = 0; d.shops = 0;
    s_dev.push_back(d);
    return s_dev.back();
}
static std::vector<std::string> myTownNames()
{
    std::vector<std::string> out;
    Faction* pf = wbPlayerFaction();
    if (!pf || !pf->warMgr) return out;
    lektor<TownBase*>& towns = pf->warMgr->myTowns;
    for (uint32_t i = 0; i < towns.size(); ++i) if (towns[i]) out.push_back(wbTownName(towns[i]));
    return out;
}
static int peopleCount()
{
    return (ou && ou->player) ? (int)ou->player->getAllPlayerCharacters().size() : 0;
}

float wbCourtTaxMult()
{
    float m = 1.0f;
    if (officerOk(OFF_FINANCE)) m += financeSkill(s_off[OFF_FINANCE]) / 150.0f;
    std::vector<std::string> towns = myTownNames();
    if (!towns.empty())
    {
        float dev = 0.0f;
        for (size_t i = 0; i < towns.size(); ++i) dev += (float)devOf(towns[i]).level;
        m += 0.1f * dev / (float)towns.size();
    }
    return m;
}

// ------------------------------------------------------------ appointing
static void appoint(int office)
{
    Character* c = ou->player->selectedCharacter.getCharacter();
    if (!c || !c->isPlayerCharacter()) { wbToast("Select one of your people first, then press the office's button."); return; }
    for (int o = 0; o < OFF_COUNT; ++o) if (s_off[o] == c) s_off[o] = NULL;   // one office each
    s_off[office] = c;
    // leave the field squad for the court squad, and stay where they are
    if (g_wb.courtSquad)
    {
        if (!s_courtSquad) s_courtSquad = ou->player->createSquad();
        Faction* pf = wbPlayerFaction();
        if (s_courtSquad && pf) c->setFaction(pf, s_courtSquad);
    }
    if (c->canTakePlayerOrdersAtThisTime()) c->addOrder(NULL, HOLD_POSITION, NULL, false, true, c->getPosition());
    wbNews((std::string(c->getName()) + " is appointed " + OFFICE_NAME[office] + ".").c_str());
}
static void dismiss()
{
    Character* c = ou->player->selectedCharacter.getCharacter();
    for (int o = 0; o < OFF_COUNT; ++o)
        if (c && s_off[o] == c)
        {
            s_off[o] = NULL;
            wbNews((std::string(c->getName()) + " is relieved as " + OFFICE_NAME[o] + ".").c_str());
            return;
        }
    wbToast("Select the officer you want to dismiss, then press Dismiss.");
}

// ------------------------------------------------------------ the army
static void march()
{
    if (!officerOk(OFF_GENERAL)) { wbToast("Appoint a General first."); return; }
    if (wbWarbandBusy()) { wbToast("The army is already on a mission. (Alt+O calls it back.)"); return; }
    Character* gen = s_off[OFF_GENERAL];
    Ogre::Vector3 gp = gen->getPosition();
    struct Cand { float d2; Character* c; };
    std::vector<Cand> cands;
    const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
    for (uint32_t i = 0; i < mine.size(); ++i)
    {
        Character* c = mine[i];
        if (!c || c == gen || wbIsOfficer(c) || c->isDead() || c->isUnconcious()) continue;
        Ogre::Vector3 p = c->getPosition();
        Cand k; k.c = c; k.d2 = (p.x - gp.x) * (p.x - gp.x) + (p.z - gp.z) * (p.z - gp.z);
        cands.push_back(k);
    }
    for (size_t i = 0; i < cands.size(); ++i)                      // nearest first
        for (size_t k = i + 1; k < cands.size(); ++k)
            if (cands[k].d2 < cands[i].d2) std::swap(cands[i], cands[k]);
    std::vector<Character*> army;
    army.push_back(gen);
    for (size_t i = 0; i < cands.size() && (int)army.size() <= s_armySize; ++i) army.push_back(cands[i].c);
    if (army.size() < 2) { wbToast("The General has no soldiers. Recruit more people or lower nothing - there is no one to send."); return; }
    wbWarbandSet(army);
    wbWarbandMarch(gen);
}

// The General recruits from the people around them.
static void generalRecruits()
{
    if (!officerOk(OFF_GENERAL)) return;
    Character* gen = s_off[OFF_GENERAL];
    if (peopleCount() >= g_wb.courtMaxPeople) return;
    Faction* pf = wbPlayerFaction();
    Ogre::Vector3 gp = gen->getPosition();
    Character* best = NULL; float bestD2 = 400.0f * 400.0f;
    auto& chars = ou->getCharacterUpdateList();
    for (auto it = chars.begin(); it != chars.end(); ++it)
    {
        Character* c = *it;
        if (!c || c->isPlayerCharacter() || c->isDead() || c->isUnconcious() || c->isDown() || !c->isHuman()) continue;
        Faction* f = c->getFaction();
        if (f && pf && f->relations && f->relations->isEnemy(pf)) continue;
        if (wbTownCount(f) >= 5) continue;                       // a great power's people stay loyal
        Ogre::Vector3 p = c->getPosition();
        float d2 = (p.x - gp.x) * (p.x - gp.x) + (p.z - gp.z) * (p.z - gp.z);
        if (d2 < bestD2) { bestD2 = d2; best = c; }
    }
    if (!best) return;
    float chance = 0.15f + wbRenown() / 400.0f + generalSkill(gen) / 300.0f;
    if (chance > 0.7f) chance = 0.7f;
    if (wbRand01() > chance) return;
    Faction* from = best->getFaction();
    std::string who = best->getName();
    if (!wbJoinBand(best)) return;
    wbFollowerJoined(best, from, false);
    wbNews(("General " + std::string(gen->getName()) + " has recruited " + who + " into the army.").c_str());
}

// The General drills the soldiers who stay near them (not officers, not fighting).
static void generalDrills()
{
    if (!officerOk(OFF_GENERAL) || g_wb.drillXp <= 0.0f) return;
    Character* gen = s_off[OFF_GENERAL];
    float mult = 0.5f + generalSkill(gen) / 60.0f;                 // a master drills better
    Ogre::Vector3 gp = gen->getPosition();
    int n = 0;
    const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
    for (uint32_t i = 0; i < mine.size(); ++i)
    {
        Character* c = mine[i];
        if (!c || c == gen || wbIsOfficer(c) || c->isDead() || c->isUnconcious() || c->isDown()) continue;
        Ogre::Vector3 p = c->getPosition();
        if ((p.x - gp.x) * (p.x - gp.x) + (p.z - gp.z) * (p.z - gp.z) > 300.0f * 300.0f) continue;
        wbTrainingGiveXp(c, g_wb.drillXp * mult);
        ++n;
    }
    if (n > 0) { char b[120]; sprintf_s(b, sizeof(b), "General %s drills %d soldiers.", gen->getName().c_str(), n); wbToast(b); }
}

// ------------------------------------------------------------ the council (every 10 minutes)
static void council()
{
    Faction* pf = wbPlayerFaction();
    if (!pf) return;
    std::vector<std::string> towns = myTownNames();
    char b[300];
    std::string report;

    // STEWARD: development, shops, food
    if (officerOk(OFF_STEWARD) && !towns.empty())
    {
        float skill = stewardSkill(s_off[OFF_STEWARD]);
        for (size_t i = 0; i < towns.size(); ++i)
        {
            TownDev& d = devOf(towns[i]);
            if (d.level < 10 && wbRand01() < 0.3f + skill / 150.0f)
            {
                ++d.level;
                int newShops = d.level / 2 + 1;
                if (newShops > d.shops)
                {
                    d.shops = newShops;
                    static const char* const SHOP[] = { "a smithy", "a bar", "a general store", "a trade post", "a clinic", "a travel shop", "an armoury", "a farm supply" };
                    sprintf_s(b, sizeof(b), "%s grows (level %d): merchants have rented a building and opened %s.",
                              towns[i].c_str(), d.level, SHOP[(int)(wbRand01() * 7.99f)]);
                    wbNews(b);
                }
            }
        }
    }
    float production = 0.0f;
    for (size_t i = 0; i < towns.size(); ++i) production += 4.0f + 2.0f * (float)devOf(towns[i]).level;
    if (officerOk(OFF_STEWARD)) production *= 1.0f + stewardSkill(s_off[OFF_STEWARD]) / 100.0f;
    if (!towns.empty())
    {
        float eaten = (float)peopleCount();
        s_food += production - eaten;
        if (s_food > 1000.0f) s_food = 1000.0f;
        if (s_food <= 0.0f)
        {
            s_food = 0.0f;
            wbFollowersAllBond(-3.0f);
            wbNews("The food stores are empty. Your people go hungry and grumble about your rule.");
        }
    }

    // FINANCE: trade, rents, collection
    if (officerOk(OFF_FINANCE))
    {
        Character* fm = s_off[OFF_FINANCE];
        float skill = financeSkill(fm);
        int shops = 0;
        for (size_t i = 0; i < towns.size(); ++i) shops += devOf(towns[i]).shops;
        int income = (int)(200.0f + skill * 15.0f) + shops * 40;
        float loyal = wbFollowerLoyalty(fm);
        int stolen = 0;
        if (loyal < 40.0f) { stolen = (int)((float)income * (40.0f - loyal) / 80.0f); income -= stolen; }
        wbPayPlayer(income);
        sprintf_s(b, sizeof(b), "Your Finance Minister brings in %d cats from trade and rents.", income);
        wbToast(b);
        if (stolen > 0 && wbRand01() < 0.3f)
            wbNews(("Rumours say " + std::string(fm->getName()) + " is skimming from the treasury...").c_str());
        CharStats* st = fm->getStats();
        if (st) st->xpStat_eventBased(STAT_SCIENCE, 0.5f);
    }
    if (officerOk(OFF_STEWARD))
    {
        CharStats* st = s_off[OFF_STEWARD]->getStats();
        if (st) { st->xpStat_eventBased(STAT_ENGINEERING, 0.3f); st->xpStat_eventBased(STAT_FARMING, 0.3f); }
    }
}

// ------------------------------------------------------------ frame
void wbCourtAction(int action)
{
    if (!ou || !ou->player) return;
    switch (action)
    {
        case 0: case 1: case 2: appoint(action); break;
        case 3: dismiss(); break;
        case 4: if (s_armySize > 1) --s_armySize; break;
        case 5: if (s_armySize < 30) ++s_armySize; break;
        case 6: march(); break;
    }
}

void wbCourtFrame()
{
    if (!ou || !ou->player) return;
    if (!s_offRestore[0].empty() || !s_offRestore[1].empty() || !s_offRestore[2].empty())
    {
        const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
        for (int o = 0; o < OFF_COUNT; ++o)
        {
            if (s_offRestore[o].empty()) continue;
            for (uint32_t k = 0; k < mine.size(); ++k)
                if (mine[k] && mine[k]->getName() == s_offRestore[o]) { s_off[o] = mine[k]; break; }
            s_offRestore[o].clear();
        }
    }
    for (int o = 0; o < OFF_COUNT; ++o)                         // the dead and the departed leave office
        if (s_off[o] && !wbIsOwned(s_off[o]))
        {
            s_off[o] = NULL;
            wbNews((std::string("The office of ") + OFFICE_NAME[o] + " is now empty.").c_str());
        }
        else if (s_off[o] && s_off[o]->isDead())
        {
            wbNews((std::string(s_off[o]->getName()) + ", your " + OFFICE_NAME[o] + ", is dead.").c_str());
            s_off[o] = NULL;
        }
    double now = wbWorldMinutes();
    if (now >= s_nextRecruit) { s_nextRecruit = now + 3.0; generalRecruits(); generalDrills(); }
    if (now >= s_nextCouncil) { s_nextCouncil = now + 10.0; council(); }
}

std::string wbCourtSummary()
{
    std::string out = "YOUR COURT  (select one of your people, then press an office button)\n\n";
    char b[300];
    for (int o = 0; o < OFF_COUNT; ++o)
    {
        if (officerOk(o))
        {
            float skill = o == OFF_FINANCE ? financeSkill(s_off[o]) : o == OFF_STEWARD ? stewardSkill(s_off[o]) : generalSkill(s_off[o]);
            sprintf_s(b, sizeof(b), "  %s: %s  (skill %d, loyalty %d)\n", OFFICE_NAME[o], s_off[o]->getName().c_str(),
                      (int)skill, (int)wbFollowerLoyalty(s_off[o]));
        }
        else sprintf_s(b, sizeof(b), "  %s: - vacant -\n", OFFICE_NAME[o]);
        out += b;
    }
    std::vector<std::string> towns = myTownNames();
    out += "\nYOUR TOWNS\n";
    if (towns.empty()) out += "  None yet. Take one with your army (General + March) or your warband (Alt+U).\n";
    for (size_t i = 0; i < towns.size(); ++i)
    {
        TownDev& d = devOf(towns[i]);
        sprintf_s(b, sizeof(b), "  %s - development %d/10, %d shops\n", towns[i].c_str(), d.level, d.shops);
        out += b;
    }
    sprintf_s(b, sizeof(b), "\nFOOD STORES: %d   (people: %d)\nTAX BONUS: +%d%%\n", (int)s_food, peopleCount(),
              (int)((wbCourtTaxMult() - 1.0f) * 100.0f + 0.5f));
    out += b;
    sprintf_s(b, sizeof(b), "\nARMY: %d soldiers will march with the General  (- / + to change, March to attack the nearest enemy town)\n", s_armySize);
    out += b;
    return out;
}

// ------------------------------------------------------------ saving
void wbCourtSave(std::vector<std::string>& out)
{
    char b[64];
    for (int o = 0; o < OFF_COUNT; ++o)
        if (officerOk(o)) { sprintf_s(b, sizeof(b), "office\t%d\t", o); out.push_back(b + wbClean(s_off[o]->getName())); }
    sprintf_s(b, sizeof(b), "court\t%.1f\t%d", s_food, s_armySize);
    out.push_back(b);
    for (size_t i = 0; i < s_dev.size(); ++i)
    {
        sprintf_s(b, sizeof(b), "\t%d\t%d", s_dev[i].level, s_dev[i].shops);
        out.push_back("dev\t" + wbClean(s_dev[i].town) + b);
    }
}
void wbCourtLoad(const std::vector<std::string>& in)
{
    for (size_t i = 0; i < in.size(); ++i)
    {
        std::vector<std::string> f = wbSplit(in[i]);
        if (f[0] == "office" && f.size() >= 3) { int o = atoi(f[1].c_str()); if (o >= 0 && o < OFF_COUNT) s_offRestore[o] = f[2]; }
        else if (f[0] == "court" && f.size() >= 3) { s_food = (float)atof(f[1].c_str()); s_armySize = atoi(f[2].c_str()); if (s_armySize < 1) s_armySize = 1; }
        else if (f[0] == "dev" && f.size() >= 4) { TownDev& d = devOf(f[1]); d.level = atoi(f[2].c_str()); d.shops = atoi(f[3].c_str()); }
    }
}
void wbCourtReset()
{
    for (int o = 0; o < OFF_COUNT; ++o) { s_off[o] = NULL; s_offRestore[o].clear(); }
    s_courtSquad = NULL; s_food = 60.0f; s_armySize = 4; s_nextCouncil = 10.0; s_nextRecruit = 5.0; s_dev.clear();
}
