// Warborn: World - contracts offered by the factions you know.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
//   HUNT     - fell N fighters of one of their enemies (anywhere).
//   DEFEND   - a town of theirs is under attack: fight at their side and
//              make sure it does not fall.
//   ASSAULT  - they are attacking a town: help them take it.
// Offers appear in the journal (J). Accept one with its button; finishing it
// earns reputation with whoever offered it. Failing a war contract costs some.

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <vector>

enum ContractKind { CK_HUNT = 0, CK_DEFEND, CK_ASSAULT };
struct Contract
{
    int         kind;
    Faction*    giver;
    Faction*    target;        // HUNT: whose fighters
    std::string giverName, targetName, townName;
    int         warId;         // DEFEND / ASSAULT
    int         need, have;
    float       reward;
    double      expires;       // offers only
};
static std::vector<Contract> s_offers;
static std::vector<Contract> s_active;
static double s_nextRefresh = 0.5;
static const int MAX_OFFERS = 4;
static const int MAX_ACTIVE = 3;

static bool willingGiver(Faction* f)
{
    Faction* pf = wbPlayerFaction();
    if (!f || !pf || f == pf || !f->relations || !ou || !ou->player) return false;
    if (!ou->player->isFactionKnown(f)) return false;
    if (f->relations->isEnemy(pf)) return false;
    return f->relations->getFactionRelation(pf) > -15.0f;
}

static void addOffer(const Contract& c)
{
    for (size_t i = 0; i < s_offers.size(); ++i)                       // no duplicates
        if (s_offers[i].kind == c.kind && s_offers[i].giver == c.giver
            && s_offers[i].target == c.target && s_offers[i].warId == c.warId) return;
    if ((int)s_offers.size() >= MAX_OFFERS) s_offers.erase(s_offers.begin());   // the oldest makes room
    s_offers.push_back(c);
}

static void makeHuntOffer(double now)
{
    if (!ou || !ou->factionMgr) return;
    const lektor<Faction*>* all = ou->factionMgr->getAllFactions();
    if (!all || all->size() == 0) return;
    for (int tries = 0; tries < 12; ++tries)
    {
        Faction* g = (*all)[(uint32_t)(wbRand01() * (float)(all->size() - 1) + 0.5f)];
        if (!willingGiver(g)) continue;
        Faction* e = (*all)[(uint32_t)(wbRand01() * (float)(all->size() - 1) + 0.5f)];
        if (!e || e == g || wbIsPlayerFaction(e) || !ou->player->isFactionKnown(e)) continue;
        if (!g->relations->isEnemy(e)) continue;
        Contract c;
        c.kind = CK_HUNT; c.giver = g; c.target = e; c.warId = 0;
        c.giverName = wbFactionName(g); c.targetName = wbFactionName(e);
        c.need = 3 + (int)(wbRand01() * 5.0f); c.have = 0;
        c.reward = 4.0f + 1.5f * (float)c.need;
        c.expires = now + 15.0;
        addOffer(c);
        return;
    }
}

void wbContractsOnWarStarted(const WbWarEvent& e)
{
    double now = wbWorldMinutes();
    if (willingGiver(e.defender))
    {
        Contract c;
        c.kind = CK_DEFEND; c.giver = e.defender; c.target = e.attacker; c.warId = e.id;
        c.giverName = e.defenderName; c.targetName = e.attackerName; c.townName = e.townName;
        c.need = 2; c.have = 0; c.reward = g_wb.repWarHelp * 1.5f; c.expires = now + g_wb.resolveMin;
        addOffer(c);
    }
    if (willingGiver(e.attacker))
    {
        Contract c;
        c.kind = CK_ASSAULT; c.giver = e.attacker; c.target = e.defender; c.warId = e.id;
        c.giverName = e.attackerName; c.targetName = e.defenderName; c.townName = e.townName;
        c.need = 2; c.have = 0; c.reward = g_wb.repWarHelp * 1.5f; c.expires = now + g_wb.resolveMin;
        addOffer(c);
    }
}

void wbContractsOnWarEnded(const WbWarEvent& e)
{
    for (size_t i = 0; i < s_offers.size(); )                          // offers for this war are gone
    {
        if (s_offers[i].warId == e.id && s_offers[i].kind != CK_HUNT) s_offers.erase(s_offers.begin() + i);
        else ++i;
    }
    for (size_t i = 0; i < s_active.size(); )
    {
        Contract& c = s_active[i];
        if (c.warId != e.id || c.kind == CK_HUNT) { ++i; continue; }
        bool won  = (c.kind == CK_DEFEND)  ? (!e.conquered && e.playerHelpedDefender >= c.need)
                                           : ( e.conquered && e.playerHelpedAttacker >= c.need);
        char b[260];
        if (won)
        {
            sprintf_s(b, sizeof(b), "Contract done: %s %s. The %s are grateful.",
                      c.kind == CK_DEFEND ? "you defended" : "you helped take", c.townName.c_str(), c.giverName.c_str());
            wbNews(b);
            wbGiveRep(c.giver, c.reward, "for the contract");
        }
        else
        {
            sprintf_s(b, sizeof(b), "Contract failed: %s. The %s remember.", c.townName.c_str(), c.giverName.c_str());
            wbNews(b);
            wbGiveRep(c.giver, -0.4f * c.reward, "for a broken promise");
        }
        s_active.erase(s_active.begin() + i);
    }
}

void wbContractsOnFelled(Faction* victimFaction)
{
    for (size_t i = 0; i < s_active.size(); )
    {
        Contract& c = s_active[i];
        if (c.kind == CK_HUNT && c.target == victimFaction)
        {
            ++c.have;
            if (c.have >= c.need)
            {
                char b[220];
                sprintf_s(b, sizeof(b), "Contract done: %d %s fighters felled for the %s.",
                          c.need, c.targetName.c_str(), c.giverName.c_str());
                wbNews(b);
                wbGiveRep(c.giver, c.reward, "for the hunt");
                s_active.erase(s_active.begin() + i);
                continue;
            }
            char t[160]; sprintf_s(t, sizeof(t), "Hunt: %d / %d %s", c.have, c.need, c.targetName.c_str());
            wbToast(t);
        }
        ++i;
    }
}

void wbContractsFrame()
{
    double now = wbWorldMinutes();
    for (size_t i = 0; i < s_offers.size(); )
    {
        if (now > s_offers[i].expires) s_offers.erase(s_offers.begin() + i);
        else ++i;
    }
    if (now >= s_nextRefresh)
    {
        s_nextRefresh = now + 4.0;
        if ((int)s_offers.size() < MAX_OFFERS) makeHuntOffer(now);
    }
}

bool wbContractsAccept(int index)
{
    if (index < 0 || index >= (int)s_offers.size()) return false;
    if ((int)s_active.size() >= MAX_ACTIVE) { wbToast("You already carry three contracts. Finish one first."); return false; }
    Contract c = s_offers[index];
    s_offers.erase(s_offers.begin() + index);
    s_active.push_back(c);
    char b[220];
    sprintf_s(b, sizeof(b), "Contract accepted for the %s.", c.giverName.c_str());
    wbToast(b);
    return true;
}

int wbContractsOfferCount() { return (int)s_offers.size(); }

static std::string describe(const Contract& c, bool active)
{
    char b[300];
    switch (c.kind)
    {
        case CK_HUNT:
            if (active) sprintf_s(b, sizeof(b), "[Hunt] Fell %d %s fighters for the %s  (%d/%d)",
                                  c.need, c.targetName.c_str(), c.giverName.c_str(), c.have, c.need);
            else        sprintf_s(b, sizeof(b), "[Hunt] The %s want %d %s fighters felled.  Reward: +%d rep",
                                  c.giverName.c_str(), c.need, c.targetName.c_str(), (int)c.reward);
            break;
        case CK_DEFEND:
            sprintf_s(b, sizeof(b), "[Defend] Help the %s hold %s against the %s.  Reward: +%d rep",
                      c.giverName.c_str(), c.townName.c_str(), c.targetName.c_str(), (int)c.reward);
            break;
        default:
            sprintf_s(b, sizeof(b), "[Assault] Help the %s take %s from the %s.  Reward: +%d rep",
                      c.giverName.c_str(), c.townName.c_str(), c.targetName.c_str(), (int)c.reward);
            break;
    }
    return std::string(b);
}

std::string wbContractsSummary()
{
    std::string out = "OFFERS (press a number button to accept)\n";
    char b[16];
    if (s_offers.empty()) out += "  No one has work for you right now. Check back later.\n";
    for (size_t i = 0; i < s_offers.size(); ++i)
    {
        sprintf_s(b, sizeof(b), "  %d. ", (int)i + 1);
        out += b; out += describe(s_offers[i], false); out += "\n";
    }
    out += "\nYOUR CONTRACTS\n";
    if (s_active.empty()) out += "  none\n";
    for (size_t i = 0; i < s_active.size(); ++i) { out += "  - "; out += describe(s_active[i], true); out += "\n"; }
    return out;
}

void wbContractsReset()
{
    s_offers.clear(); s_active.clear(); s_nextRefresh = 0.5;
}
