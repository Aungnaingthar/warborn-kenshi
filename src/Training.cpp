// Warborn: World - sparring and tournaments between your own people.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
//   T        the selected fighter spars 1 vs 1 with the nearest squadmate.
//            It is a REAL fight with blunted damage (SparDamage) and little
//            bleeding: bruises and knock-downs, not deaths. It ends when one
//            goes down or SparSeconds pass. Both gain extra experience in
//            their weapon, attack, defence and dodge; the winner gains more.
//            If the game refuses to let allies fight, they drill instead
//            (no blows, half the experience).
//   Shift+T  a tournament: every fighter of yours nearby enters a knockout
//            bracket; matches are fought one after another; the champion
//            earns renown and a big experience bonus.
// The hit hook may run on a worker thread: the list of sparring pairs it
// reads is guarded by a lock.

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Damages.h>
#include <kenshi/Enums.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <string>
#include <algorithm>

extern bool g_wbTrainPressed;
extern bool g_wbTournamentPressed;

// ------------------------------------------------------------ pairs read by the hit hook
static CRITICAL_SECTION s_lock;
static bool             s_lockReady = false;
static Character*       s_pairA[8];
static Character*       s_pairB[8];
static int              s_pairN = 0;

static void publishPairs();

void wbTrainingScaleHit(Character* victim, Character* attacker, Damages& d)
{
    if (!s_lockReady || !victim || !attacker) return;
    bool spar = false;
    EnterCriticalSection(&s_lock);
    for (int i = 0; i < s_pairN; ++i)
        if ((s_pairA[i] == victim && s_pairB[i] == attacker) || (s_pairA[i] == attacker && s_pairB[i] == victim)) { spar = true; break; }
    LeaveCriticalSection(&s_lock);
    if (!spar) return;
    d.cut *= g_wb.sparDamage; d.blunt *= g_wb.sparDamage; d.pierce *= g_wb.sparDamage;
    d.bleedMult *= 0.25f;                                   // blunted blades: bruises, not bleeding out
}

// ------------------------------------------------------------ bouts
struct Bout
{
    Character* a; Character* b;
    std::string an, bn;
    double start, nextTick;
    bool   real;           // they are really exchanging blows
    bool   checked;        // we looked whether the game let them fight
    bool   tournament;
};
static std::vector<Bout> s_bouts;
static std::vector<Character*> s_bracket;        // fighters still in the tournament (this round)
static std::vector<Character*> s_nextRound;
static bool  s_tournament = false;
static int   s_tRound = 0;
static int   s_xpLogs = 0;

static bool fit(Character* c) { return c && !c->isDead() && !c->isUnconcious() && !c->isDown(); }
static bool busy(Character* c)
{
    for (size_t i = 0; i < s_bouts.size(); ++i) if (s_bouts[i].a == c || s_bouts[i].b == c) return true;
    return false;
}
static float dist2(Character* a, Character* b)
{
    Ogre::Vector3 p = a->getPosition(), q = b->getPosition();
    float dx = p.x - q.x, dz = p.z - q.z; return dx * dx + dz * dz;
}
static Character* selectedOwn()
{
    Character* c = ou->player->selectedCharacter.getCharacter();
    return (c && c->isPlayerCharacter()) ? c : NULL;
}
static float bloodOf(Character* c) { MedicalSystem* m = c->getMedical(); return m ? m->blood : 0.0f; }

// the skill of the weapon in hand: compare the equipped-weapon skill with each weapon stat
static StatsEnumerated weaponStat(CharStats* st)
{
    static const StatsEnumerated W[] = { STAT_KATANAS, STAT_SABRES, STAT_HACKERS, STAT_HEAVYWEAPONS,
                                         STAT_BLUNT, STAT_POLEARMS, STAT_MARTIALARTS };
    float eq = st->getEquippedWeaponSkill();
    for (int i = 0; i < (int)(sizeof(W) / sizeof(W[0])); ++i)
        if (fabsf(st->getStat(W[i], false) - eq) < 0.01f) return W[i];
    return STAT_NONE;
}
static void giveXp(Character* c, float amount)
{
    CharStats* st = c ? c->getStats() : NULL;
    if (!st || amount <= 0.0f) return;
    StatsEnumerated w = weaponStat(st);
    float before = st->getStat(STAT_MELEE_ATTACK, true);
    if (w != STAT_NONE) st->xpStat_eventBased(w, amount);
    st->xpStat_eventBased(STAT_MELEE_ATTACK,  amount);
    st->xpStat_eventBased(STAT_MELEE_DEFENCE, amount);
    st->xpStat_eventBased(STAT_DODGE,         amount * 0.5f);
    if (s_xpLogs < 30)
    {
        ++s_xpLogs;
        char b[160];
        sprintf_s(b, sizeof(b), "[Warborn] training xp %.2f weaponStat=%d attack %.3f -> %.3f",
                  amount, (int)w, before, st->getStat(STAT_MELEE_ATTACK, true));
        wbLog(b);
    }
}

static void orderFight(Bout& t)
{
    if (t.a->canTakePlayerOrdersAtThisTime()) t.a->addOrder(NULL, MELEE_ATTACK, (RootObject*)t.b, false, true, t.b->getPosition());
    if (t.b->canTakePlayerOrdersAtThisTime()) t.b->addOrder(NULL, MELEE_ATTACK, (RootObject*)t.a, false, true, t.a->getPosition());
}
static void orderStop(Character* c)
{
    if (c && fit(c) && c->canTakePlayerOrdersAtThisTime())
        c->addOrder(NULL, HOLD_POSITION, NULL, false, true, c->getPosition());
}

static void startBout(Character* a, Character* b, bool tournament)
{
    Bout t;
    t.a = a; t.b = b; t.an = a->getName(); t.bn = b->getName();
    t.start = wbWorldMinutes(); t.nextTick = t.start + 0.05;
    t.real = true; t.checked = false; t.tournament = tournament;
    s_bouts.push_back(t);
    publishPairs();
    orderFight(s_bouts.back());
    wbToast(("Spar: " + t.an + " vs " + t.bn + ". Blunted blades - bruises, not graves.").c_str());
}

static void endBout(size_t i, Character* winner, const char* how)
{
    Bout t = s_bouts[i];
    s_bouts.erase(s_bouts.begin() + i);
    publishPairs();
    orderStop(t.a); orderStop(t.b);
    float minutes = (float)(wbWorldMinutes() - t.start);
    float k = t.real ? 1.0f : 0.5f;
    float bonus = g_wb.sparXp * k * (1.0f + minutes);       // a longer bout teaches more
    giveXp(t.a, bonus * (winner == t.a ? 1.5f : 1.0f));
    giveXp(t.b, bonus * (winner == t.b ? 1.5f : 1.0f));
    std::string msg;
    if (winner) msg = (winner == t.a ? t.an : t.bn) + " wins the spar against " + (winner == t.a ? t.bn : t.an) + " (" + how + ").";
    else        msg = t.an + " and " + t.bn + " finish their training (" + how + ").";
    wbToast(msg.c_str());
    if (t.tournament)
    {
        Character* adv = winner ? winner : (bloodOf(t.a) >= bloodOf(t.b) ? t.a : t.b);
        s_nextRound.push_back(adv);
        wbNews(("Tournament: " + std::string(adv == t.a ? t.an : t.bn) + " advances.").c_str());
    }
}

static void startTournament()
{
    Character* host = selectedOwn();
    if (!host) { wbToast("Select one of your fighters to call a tournament around them."); return; }
    if (s_tournament) { wbToast("A tournament is already running."); return; }
    std::vector<Character*> entrants;
    const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
    for (uint32_t i = 0; i < mine.size() && entrants.size() < 16; ++i)
    {
        Character* c = mine[i];
        if (!fit(c) || busy(c) || dist2(c, host) > 200.0f * 200.0f) continue;
        entrants.push_back(c);
    }
    if (entrants.size() < 2) { wbToast("A tournament needs at least two of your fighters nearby."); return; }
    std::random_shuffle(entrants.begin(), entrants.end());
    s_bracket = entrants; s_nextRound.clear(); s_tournament = true; s_tRound = 1;
    char b[160]; sprintf_s(b, sizeof(b), "A tournament begins! %d fighters enter the ring.", (int)entrants.size());
    wbNews(b);
}

static void tournamentStep()
{
    if (!s_tournament || !s_bouts.empty()) return;          // one match at a time
    // drop anyone who can't fight any more (they forfeit)
    for (size_t i = 0; i < s_bracket.size(); )
    {
        if (fit(s_bracket[i])) ++i;
        else s_bracket.erase(s_bracket.begin() + i);
    }
    if (s_bracket.size() >= 2)
    {
        Character* a = s_bracket[0]; Character* b = s_bracket[1];
        s_bracket.erase(s_bracket.begin(), s_bracket.begin() + 2);
        startBout(a, b, true);
        return;
    }
    if (s_bracket.size() == 1) { s_nextRound.push_back(s_bracket[0]); s_bracket.clear(); }   // a bye
    if (s_nextRound.size() >= 2)
    {
        s_bracket = s_nextRound; s_nextRound.clear(); ++s_tRound;
        char b[80]; sprintf_s(b, sizeof(b), "Tournament: round %d - %d fighters remain.", s_tRound, (int)s_bracket.size());
        wbNews(b);
        return;
    }
    if (s_nextRound.size() == 1)
    {
        Character* champ = s_nextRound[0];
        std::string name = champ->getName();
        wbNews((name + " is the CHAMPION of the tournament!").c_str());
        giveXp(champ, g_wb.sparXp * 4.0f);
        wbAddRenown(3.0f, "your champion's victory");
    }
    s_tournament = false; s_bracket.clear(); s_nextRound.clear();
}

static void startSpar()
{
    Character* a = selectedOwn();
    if (!a) { wbToast("Select one of your fighters, then press the training key."); return; }
    for (size_t i = 0; i < s_bouts.size(); ++i)
        if (s_bouts[i].a == a || s_bouts[i].b == a) { endBout(i, NULL, "called off"); return; }
    if (!fit(a)) { wbToast("They are in no shape to train."); return; }
    Character* best = NULL; float bestD2 = 150.0f * 150.0f;
    const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
    for (uint32_t i = 0; i < mine.size(); ++i)
    {
        Character* c = mine[i];
        if (!fit(c) || c == a || busy(c)) continue;
        float d2 = dist2(a, c);
        if (d2 < bestD2) { bestD2 = d2; best = c; }
    }
    if (!best) { wbToast("No sparring partner nearby (another of your people within ~15 m)."); return; }
    startBout(a, best, false);
}

void wbTrainingFrame()
{
    if (!ou || !ou->player) return;
    if (g_wbTournamentPressed) startTournament();
    else if (g_wbTrainPressed) startSpar();
    double now = wbWorldMinutes();
    for (size_t i = 0; i < s_bouts.size(); )
    {
        Bout& t = s_bouts[i];
        bool aUp = fit(t.a), bUp = fit(t.b);
        if (!aUp || !bUp)
        {
            Character* w = (aUp && !bUp) ? t.a : ((bUp && !aUp) ? t.b : NULL);
            endBout(i, w, "knocked down"); continue;
        }
        double secs = (now - t.start) * 60.0;
        if (secs >= g_wb.sparSeconds)
        {
            Character* w = bloodOf(t.a) > bloodOf(t.b) + 1.0f ? t.a : (bloodOf(t.b) > bloodOf(t.a) + 1.0f ? t.b : NULL);
            endBout(i, w, "time"); continue;
        }
        if (!t.checked && secs >= 5.0)
        {
            t.checked = true;
            CombatClass* ca = t.a->getCombatClass();
            CombatClass* cb = t.b->getCombatClass();
            bool fighting = (ca && ca->currentTarget == t.b) || (cb && cb->currentTarget == t.a);
            if (!fighting)
            {
                t.real = false;
                wbLog("[Warborn] spar: the game won't let allies fight - drilling instead");
                wbToast(("No blows allowed here - " + t.an + " and " + t.bn + " drill together instead.").c_str());
                t.a->playerMoveOrderDefault(NULL, NULL, t.b->getPosition());
            }
        }
        if (t.real && now >= t.nextTick && secs < 5.0) orderFight(t);   // make sure they engage
        if (now >= t.nextTick) t.nextTick = now + 0.05;
        ++i;
    }
    tournamentStep();
}

static void publishPairs()
{
    if (!s_lockReady) return;
    EnterCriticalSection(&s_lock);
    s_pairN = 0;
    for (size_t i = 0; i < s_bouts.size() && s_pairN < 8; ++i)
    { s_pairA[s_pairN] = s_bouts[i].a; s_pairB[s_pairN] = s_bouts[i].b; ++s_pairN; }
    LeaveCriticalSection(&s_lock);
}

std::string wbTrainingSummary()
{
    std::string out;
    for (size_t i = 0; i < s_bouts.size(); ++i)
        out += "  Sparring: " + s_bouts[i].an + " vs " + s_bouts[i].bn + (s_bouts[i].real ? "\n" : " (drill)\n");
    if (s_tournament)
    {
        char b[80]; sprintf_s(b, sizeof(b), "  Tournament: round %d\n", s_tRound);
        out += b;
    }
    if (out.empty()) out = "  Nobody is training. (T spar, Shift+T tournament)\n";
    return out;
}

void wbTrainingInit()
{
    if (!s_lockReady) { InitializeCriticalSection(&s_lock); s_lockReady = true; }
}
void wbTrainingReset()
{
    s_bouts.clear(); s_bracket.clear(); s_nextRound.clear(); s_tournament = false;
    publishPairs();
}
