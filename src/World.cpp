// Warborn: World - faction ambitions, planned wars, conquest, the strongest power.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// Wars are PLANNED, not random:
//   1. A faction with ambition (personality + strength + grudges) picks a goal:
//        revenge   - retake a town it lost
//        opportunity - an enemy already fighting someone else
//        ambition  - the closest, weakest enemy town
//      Small clans (1-2 towns) are the hungriest: they can rise to rule.
//   2. PLANNING: rumours spread that they are gathering forces (PlanMin).
//   3. MARCH: Kenshi's own war campaign is launched against the town, so real
//      squads walk out and fight.
//   4. OUTCOME (ResolveMin later): the assault takes the town or is driven
//      off. Strength, a defender's last stand and the PLAYER'S help decide it.
//      A conquered town changes owner; the loser holds a grudge.

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/FactionWarMgr.h>
#include <kenshi/Town.h>
#include <ogre/OgreVector3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <algorithm>

// ------------------------------------------------------------ faction minds
struct FacMind
{
    Faction*  f;
    float     aggression;     // 0.2 .. 1.0, fixed per faction (its personality)
    Faction*  grudge;         // who took a town from us
    TownBase* lostTown;
    double    grudgeUntil;
};
static std::vector<FacMind> s_minds;
static double s_nextMindRefresh = 0.0;

static float personality(const std::string& name)
{
    unsigned int h = 2166136261u;                       // FNV-1a: same faction, same personality
    for (size_t i = 0; i < name.size(); ++i) { h ^= (unsigned char)name[i]; h *= 16777619u; }
    return 0.2f + 0.8f * (float)(h % 1000) / 999.0f;
}
static FacMind* mindOf(Faction* f)
{
    for (size_t i = 0; i < s_minds.size(); ++i) if (s_minds[i].f == f) return &s_minds[i];
    return NULL;
}
static void refreshMinds()
{
    if (!ou || !ou->factionMgr) return;
    const lektor<Faction*>* all = ou->factionMgr->getAllFactions();
    if (!all) return;
    for (uint32_t i = 0; i < all->size(); ++i)
    {
        Faction* f = (*all)[i];
        if (!f || mindOf(f)) continue;
        FacMind m;
        m.f = f; m.aggression = personality(wbFactionName(f));
        m.grudge = NULL; m.lostTown = NULL; m.grudgeUntil = 0.0;
        s_minds.push_back(m);
    }
}

// ------------------------------------------------------------ towns
static bool townStillExists(TownBase* t, Faction** ownerOut)
{
    if (!t) return false;
    for (size_t i = 0; i < s_minds.size(); ++i)
    {
        Faction* f = s_minds[i].f;
        if (!f || !f->warMgr) continue;
        lektor<TownBase*>& towns = f->warMgr->myTowns;
        for (uint32_t k = 0; k < towns.size(); ++k)
            if (towns[k] == t) { if (ownerOut) *ownerOut = f; return true; }
    }
    return false;
}
static float distXZ(const Ogre::Vector3& a, const Ogre::Vector3& b)
{
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}
static TownBase* nearestOwnTown(Faction* f, const Ogre::Vector3& to)
{
    if (!f || !f->warMgr) return NULL;
    TownBase* best = NULL; float bestD = 1e30f;
    lektor<TownBase*>& towns = f->warMgr->myTowns;
    for (uint32_t k = 0; k < towns.size(); ++k)
    {
        TownBase* t = towns[k];
        if (!t) continue;
        float d = distXZ(t->getPosition(), to);
        if (d < bestD) { bestD = d; best = t; }
    }
    return best;
}

// ------------------------------------------------------------ war plans
enum { STAGE_PLANNING = 0, STAGE_MARCHING = 1 };
struct WarPlan
{
    WbWarEvent ev;
    TownBase*  home;
    int        stage;
    double     stageAt;
    const char* reason;
};
static std::vector<WarPlan> s_plans;
static int    s_nextWarId   = 1;
static double s_nextPlanAt  = 1.0;
static Faction* s_strongest = NULL;
static bool   s_strongestKnown = false;

// ------------------------------------------------------------ diplomacy
// Pacts are Warborn's own agreements between NPC factions (the game's base
// relations are left alone, so nothing breaks when a pact ends):
//   TRUCE     - two factions worn out by repeated wars stop attacking each other.
//   COALITION - when one power grows too big, two of its enemies join forces
//               against it: they won't fight each other and go after its towns.
enum { PACT_TRUCE = 0, PACT_COALITION = 1 };
struct Pact { Faction* a; Faction* b; int kind; double until; Faction* against; };
static std::vector<Pact> s_pacts;
struct PairWars { Faction* a; Faction* b; int wars; double last; };
static std::vector<PairWars> s_pairWars;
static double s_nextDiplomacy = 3.0;

static bool samePair(Faction* a, Faction* b, Faction* x, Faction* y) { return (a == x && b == y) || (a == y && b == x); }
static Pact* findPact(Faction* a, Faction* b, int kind)
{
    double now = wbWorldMinutes();
    for (size_t i = 0; i < s_pacts.size(); ++i)
        if (s_pacts[i].kind == kind && now < s_pacts[i].until && samePair(s_pacts[i].a, s_pacts[i].b, a, b)) return &s_pacts[i];
    return NULL;
}
static bool atPeace(Faction* a, Faction* b) { return findPact(a, b, PACT_TRUCE) || findPact(a, b, PACT_COALITION); }
static Faction* coalitionEnemy(Faction* f)
{
    double now = wbWorldMinutes();
    for (size_t i = 0; i < s_pacts.size(); ++i)
        if (s_pacts[i].kind == PACT_COALITION && now < s_pacts[i].until && (s_pacts[i].a == f || s_pacts[i].b == f))
            return s_pacts[i].against;
    return NULL;
}
static void countWar(Faction* a, Faction* b)
{
    double now = wbWorldMinutes();
    for (size_t i = 0; i < s_pairWars.size(); ++i)
        if (samePair(s_pairWars[i].a, s_pairWars[i].b, a, b))
        {
            if (now - s_pairWars[i].last > 60.0) s_pairWars[i].wars = 0;   // old wars are forgotten
            ++s_pairWars[i].wars; s_pairWars[i].last = now; return;
        }
    PairWars p; p.a = a; p.b = b; p.wars = 1; p.last = now;
    s_pairWars.push_back(p);
}

static bool isTargeted(TownBase* t)
{
    for (size_t i = 0; i < s_plans.size(); ++i) if (s_plans[i].ev.town == t) return true;
    return false;
}
static bool isAttacking(Faction* f)
{
    for (size_t i = 0; i < s_plans.size(); ++i) if (s_plans[i].ev.attacker == f) return true;
    return false;
}
static bool isDefending(Faction* f)
{
    for (size_t i = 0; i < s_plans.size(); ++i) if (s_plans[i].ev.defender == f) return true;
    return false;
}
static bool canWage(Faction* f)
{
    return f && !wbIsPlayerFaction(f) && f->warMgr && f->relations
        && f->warMgr->possibleCampaigns.size() > 0 && f->warMgr->myTowns.size() > 0;
}

// Pick who goes to war: personality, size, grudges; small clans are hungry.
static FacMind* pickAggressor(double now)
{
    float total = 0.0f;
    std::vector<float> w(s_minds.size(), 0.0f);
    for (size_t i = 0; i < s_minds.size(); ++i)
    {
        FacMind& m = s_minds[i];
        if (!canWage(m.f) || isAttacking(m.f)) continue;
        int towns = wbTownCount(m.f);
        float wt = m.aggression * (1.0f + 0.15f * (float)towns);
        if (towns <= 2) wt *= 1.5f;                              // a small clan with big dreams
        if (m.grudge && now < m.grudgeUntil) wt *= 2.5f;          // revenge burns
        if (coalitionEnemy(m.f)) wt *= 1.5f;                      // the coalition is on the march
        w[i] = wt; total += wt;
    }
    if (total <= 0.0f) return NULL;
    float r = wbRand01() * total;
    for (size_t i = 0; i < s_minds.size(); ++i) { r -= w[i]; if (r <= 0.0f && w[i] > 0.0f) return &s_minds[i]; }
    return NULL;
}

static bool pickTarget(FacMind& m, double now, TownBase** townOut, Faction** defOut, const char** reasonOut)
{
    Faction* me = m.f;
    // 1. revenge: retake what was lost
    if (m.grudge && m.lostTown && now < m.grudgeUntil && !isTargeted(m.lostTown))
    {
        Faction* owner = NULL;
        if (townStillExists(m.lostTown, &owner) && owner == m.grudge)
        { *townOut = m.lostTown; *defOut = owner; *reasonOut = "to take back what was stolen"; return true; }
    }
    // 2. the best enemy town: close, weak, distracted
    Faction* pf = wbPlayerFaction();
    Ogre::Vector3 from(0, 0, 0);
    {
        lektor<TownBase*>& mine = me->warMgr->myTowns;
        float n = 0.0f;
        for (uint32_t k = 0; k < mine.size(); ++k) if (mine[k]) { from += mine[k]->getPosition(); n += 1.0f; }
        if (n > 0.0f) from /= n;
    }
    TownBase* best = NULL; Faction* bestDef = NULL; float bestScore = 1e30f; bool bestOpp = false;
    for (size_t i = 0; i < s_minds.size(); ++i)
    {
        Faction* g = s_minds[i].f;
        if (!g || g == me || !g->warMgr) continue;
        if (g == pf && !g_wb.attackPlayerTowns) continue;
        if (!me->relations->isEnemy(g)) continue;
        if (atPeace(me, g)) continue;                       // truce or coalition partner
        bool coalitionTarget = (coalitionEnemy(me) == g);
        lektor<TownBase*>& towns = g->warMgr->myTowns;
        bool distracted = isDefending(g) || isAttacking(g);
        for (uint32_t k = 0; k < towns.size(); ++k)
        {
            TownBase* t = towns[k];
            if (!t || isTargeted(t)) continue;
            float d = distXZ(t->getPosition(), from);
            float score = d / 5000.0f + 1.5f * (float)wbTownCount(g) + 2.0f * wbRand01();
            if (distracted) score -= 3.0f;
            if (coalitionTarget) score -= 5.0f;                 // the coalition's sworn enemy
            if (score < bestScore) { bestScore = score; best = t; bestDef = g; bestOpp = distracted; }
        }
    }
    if (!best) return false;
    *townOut = best; *defOut = bestDef;
    if (coalitionEnemy(me) == bestDef) *reasonOut = "as the coalition swore";
    else *reasonOut = bestOpp ? "while their enemy is busy fighting elsewhere" : "to grow their power";
    return true;
}

static void startPlan(double now)
{
    FacMind* m = pickAggressor(now);
    if (!m) { wbLog("[Warborn] war: nobody ready to march"); return; }
    TownBase* town = NULL; Faction* def = NULL; const char* reason = "";
    if (!pickTarget(*m, now, &town, &def, &reason)) { wbLog("[Warborn] war: no reachable enemy town"); return; }
    TownBase* home = nearestOwnTown(m->f, town->getPosition());
    if (!home) return;

    WarPlan p;
    p.ev.id = s_nextWarId++;
    p.ev.attacker = m->f; p.ev.defender = def; p.ev.town = town;
    p.ev.attackerName = wbFactionName(m->f); p.ev.defenderName = wbFactionName(def); p.ev.townName = wbTownName(town);
    p.ev.conquered = false; p.ev.playerHelpedAttacker = 0; p.ev.playerHelpedDefender = 0;
    p.home = home; p.stage = STAGE_PLANNING; p.stageAt = now; p.reason = reason;
    s_plans.push_back(p);

    WbVars v; v.A = p.ev.attackerName; v.D = p.ev.defenderName; v.T = p.ev.townName; v.H = wbTownName(home); v.R = reason;
    wbNews(wbLine(LINE_RUMOUR, v).c_str());
}

static void launch(WarPlan& p, double now)
{
    Faction* owner = NULL;
    if (!townStillExists(p.ev.town, &owner) || owner != p.ev.defender) { p.stage = -1; return; }
    lektor<CampaignData*>& camps = p.ev.attacker->warMgr->possibleCampaigns;
    if (camps.size() == 0) { p.stage = -1; return; }
    CampaignData* data = camps[(uint32_t)(wbRand01() * (float)(camps.size() - 1) + 0.5f)];
    CampaignInstance* inst = p.ev.attacker->warMgr->triggerCampaign(p.ev.town, data, p.home);
    char lb[200];
    sprintf_s(lb, sizeof(lb), "[Warborn] war %d launch %s -> %s campaign=%p instance=%p",
              p.ev.id, p.ev.attackerName.c_str(), p.ev.townName.c_str(), (void*)data, (void*)inst);
    wbLogAlways(lb);
    if (!inst) { p.stage = -1; return; }
    p.stage = STAGE_MARCHING; p.stageAt = now;
    countWar(p.ev.attacker, p.ev.defender);
    WbVars v; v.A = p.ev.attackerName; v.D = p.ev.defenderName; v.T = p.ev.townName; v.H = wbTownName(p.home);
    wbNews(wbLine(LINE_WAR_START, v).c_str());
    wbContractsOnWarStarted(p.ev);
}

static void announceStrongest(bool quiet)
{
    Faction* best = NULL; int bestTowns = 0;
    for (size_t i = 0; i < s_minds.size(); ++i)
    {
        Faction* f = s_minds[i].f;
        if (!f || wbIsPlayerFaction(f)) continue;
        int n = wbTownCount(f);
        if (n > bestTowns) { bestTowns = n; best = f; }
    }
    if (best && best != s_strongest && bestTowns >= 3 && s_strongestKnown && !quiet)
    {
        char n[16]; sprintf_s(n, sizeof(n), "%d", bestTowns);
        WbVars v; v.A = wbFactionName(best); v.X = n;
        wbNews(wbLine(LINE_STRONGEST, v).c_str());
    }
    s_strongest = best; s_strongestKnown = true;
}

static void resolve(WarPlan& p, double now)
{
    WbWarEvent& e = p.ev;
    Faction* owner = NULL;
    bool exists = townStillExists(e.town, &owner);
    int atkTowns = wbTownCount(e.attacker), defTowns = wbTownCount(e.defender);
    float chance = g_wb.conquestBase * (float)(atkTowns + 2) / (float)(defTowns + 2);
    chance += 0.06f * (float)e.playerHelpedAttacker - 0.06f * (float)e.playerHelpedDefender;
    if (defTowns <= 1) chance *= 0.6f;                         // a last stand is fierce
    if (chance < 0.05f) chance = 0.05f;
    if (chance > 0.85f) chance = 0.85f;
    bool taken = g_wb.conquest && exists && owner == e.defender && wbRand01() < chance;

    char lb[200];
    sprintf_s(lb, sizeof(lb), "[Warborn] war %d resolve chance=%.2f taken=%d helpAtk=%d helpDef=%d",
              e.id, chance, taken ? 1 : 0, e.playerHelpedAttacker, e.playerHelpedDefender);
    wbLogAlways(lb);

    WbVars v; v.A = e.attackerName; v.D = e.defenderName; v.T = e.townName; v.H = wbTownName(p.home);
    if (taken)
    {
        e.town->setFaction(e.attacker, NULL);
        e.conquered = true;
        wbNews(wbLine(LINE_TOWN_FELL, v).c_str());
        FacMind* loser = mindOf(e.defender);
        if (loser) { loser->grudge = e.attacker; loser->lostTown = e.town; loser->grudgeUntil = now + 90.0; }
        if (e.attacker->relations) e.attacker->relations->affectRelations(e.defender, -15.0f, 1.0f);
        if (e.defender->relations) e.defender->relations->affectRelations(e.attacker, -25.0f, 1.0f);
        announceStrongest(false);
    }
    else
    {
        wbNews(wbLine(LINE_TOWN_HELD, v).c_str());
    }
    // the player's part in it
    if (e.playerHelpedDefender > 0)
    {
        float k = e.playerHelpedDefender >= 3 ? 1.0f : (float)e.playerHelpedDefender / 3.0f;
        wbGiveRep(e.defender, g_wb.repWarHelp * k, "for fighting at their side");
        wbAddRenown(3.0f * k + (e.conquered ? 0.0f : 2.0f), "for your part in the siege");
        wbGiveRep(e.attacker, -0.5f * g_wb.repWarHelp * k, "for fighting against them");
    }
    if (e.playerHelpedAttacker > 0)
    {
        float k = e.playerHelpedAttacker >= 3 ? 1.0f : (float)e.playerHelpedAttacker / 3.0f;
        wbGiveRep(e.attacker, g_wb.repWarHelp * k, "for joining their assault");
        wbAddRenown(3.0f * k + (e.conquered ? 3.0f : 0.0f), "for your part in the assault");
        wbGiveRep(e.defender, -0.5f * g_wb.repWarHelp * k, "for attacking their town");
    }
    wbContractsOnWarEnded(e);
}

static void diplomacy(double now)
{
    // expire pacts (with news)
    for (size_t i = 0; i < s_pacts.size(); )
    {
        if (now >= s_pacts[i].until)
        {
            char b[220];
            if (s_pacts[i].kind == PACT_TRUCE)
                sprintf_s(b, sizeof(b), "The truce between the %s and the %s has ended.",
                          wbFactionName(s_pacts[i].a).c_str(), wbFactionName(s_pacts[i].b).c_str());
            else
                sprintf_s(b, sizeof(b), "The coalition of the %s and the %s has broken apart.",
                          wbFactionName(s_pacts[i].a).c_str(), wbFactionName(s_pacts[i].b).c_str());
            wbNews(b);
            s_pacts.erase(s_pacts.begin() + i);
        }
        else ++i;
    }
    // TRUCE: worn out by repeated wars
    for (size_t i = 0; i < s_pairWars.size(); ++i)
    {
        PairWars& pw = s_pairWars[i];
        if (pw.wars < 2 || now - pw.last > 60.0 || atPeace(pw.a, pw.b)) continue;
        if (wbRand01() > 0.35f) continue;
        Pact t; t.a = pw.a; t.b = pw.b; t.kind = PACT_TRUCE; t.until = now + 30.0 + 30.0 * wbRand01(); t.against = NULL;
        s_pacts.push_back(t);
        pw.wars = 0;
        WbVars v; v.A = wbFactionName(pw.a); v.D = wbFactionName(pw.b);
        wbNews(wbLine(LINE_TRUCE, v).c_str());
        return;                                              // one big event at a time
    }
    // COALITION: balance of power against the strongest
    Faction* top = NULL; int topTowns = 0, second = 0;
    for (size_t i = 0; i < s_minds.size(); ++i)
    {
        Faction* f = s_minds[i].f;
        if (!f || wbIsPlayerFaction(f)) continue;
        int n = wbTownCount(f);
        if (n > topTowns) { second = topTowns; topTowns = n; top = f; }
        else if (n > second) second = n;
    }
    if (!top || topTowns < 4 || (float)topTowns < 1.5f * (float)second) return;
    if (coalitionEnemy(top)) return;
    for (size_t i = 0; i < s_minds.size(); ++i)
    {
        Faction* a = s_minds[i].f;
        if (!canWage(a) || a == top || coalitionEnemy(a) || !a->relations->isEnemy(top)) continue;
        for (size_t k = i + 1; k < s_minds.size(); ++k)
        {
            Faction* b = s_minds[k].f;
            if (!canWage(b) || b == top || coalitionEnemy(b) || !b->relations->isEnemy(top)) continue;
            if (a->relations->isEnemy(b) && !findPact(a, b, PACT_TRUCE)) continue;  // can't stand each other
            if (wbRand01() > 0.5f) continue;
            Pact c; c.a = a; c.b = b; c.kind = PACT_COALITION; c.until = now + 45.0 + 30.0 * wbRand01(); c.against = top;
            s_pacts.push_back(c);
            WbVars v; v.A = wbFactionName(a); v.D = wbFactionName(b); v.T = wbFactionName(top);
            wbNews(wbLine(LINE_COALITION, v).c_str());
            return;
        }
    }
}

std::string wbDiplomacySummary()
{
    double now = wbWorldMinutes();
    std::string out; char b[260];
    for (size_t i = 0; i < s_pacts.size(); ++i)
    {
        const Pact& p = s_pacts[i];
        if (now >= p.until) continue;
        if (p.kind == PACT_TRUCE)
            sprintf_s(b, sizeof(b), "  - Truce: %s and %s (~%d min left)\n", wbFactionName(p.a).c_str(),
                      wbFactionName(p.b).c_str(), (int)(p.until - now));
        else
            sprintf_s(b, sizeof(b), "  - Coalition: %s and %s against the %s (~%d min left)\n", wbFactionName(p.a).c_str(),
                      wbFactionName(p.b).c_str(), wbFactionName(p.against).c_str(), (int)(p.until - now));
        out += b;
    }
    if (out.empty()) out = "  No pacts. Every faction stands alone.\n";
    return out;
}

// ------------------------------------------------------------ frame
void wbWorldFrame()
{
    if (!g_wb.wars) return;
    double now = wbWorldMinutes();
    if (now >= s_nextMindRefresh)
    {
        s_nextMindRefresh = now + 1.0;
        refreshMinds();
        if (!s_strongestKnown && !s_minds.empty()) announceStrongest(true);
    }
    if (now >= s_nextDiplomacy)
    {
        s_nextDiplomacy = now + 5.0;
        diplomacy(now);
    }
    if (now >= s_nextPlanAt)
    {
        s_nextPlanAt = now + g_wb.warIntervalMin * (0.7f + 0.6f * wbRand01());
        if ((int)s_plans.size() < g_wb.maxWars) startPlan(now);
    }
    for (size_t i = 0; i < s_plans.size(); ++i)
    {
        WarPlan& p = s_plans[i];
        if (p.stage == STAGE_PLANNING && now - p.stageAt >= g_wb.planMin) launch(p, now);
        else if (p.stage == STAGE_MARCHING && now - p.stageAt >= g_wb.resolveMin) { resolve(p, now); p.stage = -1; }
    }
    for (size_t i = 0; i < s_plans.size(); )
    {
        if (s_plans[i].stage < 0) s_plans.erase(s_plans.begin() + i);
        else ++i;
    }
}

void wbWorldOnFelled(Faction* victimFaction, float x, float z)
{
    if (!victimFaction) return;
    Ogre::Vector3 at(x, 0.0f, z);
    for (size_t i = 0; i < s_plans.size(); ++i)
    {
        WarPlan& p = s_plans[i];
        if (p.stage != STAGE_MARCHING) continue;
        if (!townStillExists(p.ev.town, NULL)) continue;
        if (distXZ(p.ev.town->getPosition(), at) > 2500.0f) continue;   // ~250 m around the town
        if (victimFaction == p.ev.attacker) ++p.ev.playerHelpedDefender;
        else if (victimFaction == p.ev.defender) ++p.ev.playerHelpedAttacker;
    }
}

void wbWorldReset()
{
    s_minds.clear(); s_plans.clear(); s_pacts.clear(); s_pairWars.clear(); s_nextDiplomacy = 3.0;
    s_nextMindRefresh = 0.0; s_nextPlanAt = 1.0;
    s_strongest = NULL; s_strongestKnown = false;
}

std::string wbWorldSummary()
{
    if (s_plans.empty()) return std::string("  The land is quiet... for now.\n");
    std::string out;
    double now = wbWorldMinutes();
    char b[300];
    for (size_t i = 0; i < s_plans.size(); ++i)
    {
        const WarPlan& p = s_plans[i];
        if (p.stage == STAGE_PLANNING)
            sprintf_s(b, sizeof(b), "  - The %s are preparing to attack %s (%s).\n",
                      p.ev.attackerName.c_str(), p.ev.townName.c_str(), p.ev.defenderName.c_str());
        else
        {
            int left = (int)(g_wb.resolveMin - (now - p.stageAt) + 0.5);
            if (left < 0) left = 0;
            sprintf_s(b, sizeof(b), "  - SIEGE of %s: %s vs %s (decided in ~%d min)\n",
                      p.ev.townName.c_str(), p.ev.attackerName.c_str(), p.ev.defenderName.c_str(), left);
        }
        out += b;
    }
    return out;
}

struct PowerRow { int towns; std::string name; };
static bool powerLess(const PowerRow& a, const PowerRow& b) { return a.towns > b.towns; }
std::string wbPowerRanking()
{
    std::vector<PowerRow> rows;
    for (size_t i = 0; i < s_minds.size(); ++i)
    {
        Faction* f = s_minds[i].f;
        if (!f || wbIsPlayerFaction(f)) continue;
        int n = wbTownCount(f);
        if (n <= 0) continue;
        PowerRow r; r.towns = n; r.name = wbFactionName(f);
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), powerLess);
    std::string out;
    char b[160];
    for (size_t i = 0; i < rows.size() && i < 8; ++i)
    {
        sprintf_s(b, sizeof(b), "  %d. %s - %d towns\n", (int)i + 1, rows[i].name.c_str(), rows[i].towns);
        out += b;
    }
    if (out.empty()) out = "  (no information yet)\n";
    return out;
}
