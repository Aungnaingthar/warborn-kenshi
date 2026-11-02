// Warborn: World - fair battles (no 10-on-1 dogpiles).
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// Vanilla AI often has a whole squad pile onto one victim. Here, when the
// game's AI picks an attack target that already has MaxAttackersPerTarget
// others on it, the fighter is sent to the nearest enemy who is less busy -
// so a 10 vs 10 becomes a battle line of duels and small knots, not a mob.
// Fighters keep their current opponent while it is still fair (no
// flip-flopping). Your own people follow your explicit orders unless
// FairForYourPeople is on.
//
// The AI may choose targets on a worker thread: everything here is guarded
// by one lock and only READS game state.

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Character.h>
#include <kenshi/combat/CombatClass.h>
#include <core/Functions.h>
#include <ogre/OgreVector3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static bool  s_fair          = true;
static int   s_maxPerTarget  = 2;
static float s_reassignRange = 300.0f;   // game units (~30 m)
static bool  s_fairForPlayer = false;

struct Engage { Character* who; Character* target; ULONGLONG at; };
static const int MAX_ENGAGE = 768;
static Engage           s_eng[MAX_ENGAGE];
static int              s_engN = 0;
static CRITICAL_SECTION s_lock;
static volatile LONG    s_redirectLogs = 0;
static const ULONGLONG  FRESH_MS = 1500;   // short: fighters far away can be unloaded

static bool usable(Character* c)
{
    return c && !c->isDead() && !c->isUnconcious() && !c->isDown();
}
static int attackersOn(Character* t, Character* except, ULONGLONG now)
{
    int n = 0;
    for (int i = 0; i < s_engN; ++i)
        if (s_eng[i].target == t && s_eng[i].who != except && now - s_eng[i].at < FRESH_MS) ++n;
    return n;
}
static Engage* findEngage(Character* who)
{
    for (int i = 0; i < s_engN; ++i) if (s_eng[i].who == who) return &s_eng[i];
    return NULL;
}
static void record(Character* who, Character* target, ULONGLONG now)
{
    Engage* e = findEngage(who);
    if (e) { e->target = target; e->at = now; return; }
    if (s_engN >= MAX_ENGAGE)                                    // drop stale entries
    {
        int keep = 0;
        for (int i = 0; i < s_engN; ++i) if (now - s_eng[i].at < FRESH_MS) s_eng[keep++] = s_eng[i];
        s_engN = keep;
        if (s_engN >= MAX_ENGAGE) s_engN = MAX_ENGAGE / 2;       // still full: forget the oldest half
    }
    s_eng[s_engN].who = who; s_eng[s_engN].target = target; s_eng[s_engN].at = now;
    ++s_engN;
}
static float dist2(Character* a, Character* b)
{
    Ogre::Vector3 p = a->getPosition(), q = b->getPosition();
    float dx = p.x - q.x, dz = p.z - q.z;
    return dx * dx + dz * dz;
}

static Character* (*s_chooseOrig)(CombatClass*) = NULL;
static Character* choose_hook(CombatClass* cc)
{
    Character* t = s_chooseOrig(cc);
    if (!s_fair || !cc || !t) return t;
    Character* me = cc->me;
    if (!me || !usable(me)) return t;
    if (me->isPlayerCharacter() && !s_fairForPlayer) return t;

    ULONGLONG now = GetTickCount64();
    Character* pick = t;
    EnterCriticalSection(&s_lock);
    if (attackersOn(t, me, now) >= s_maxPerTarget)
    {
        // 1. keep the opponent we already had, if that fight is still fair
        Engage* mine = findEngage(me);
        Character* prev = (mine && now - mine->at < FRESH_MS) ? mine->target : NULL;
        if (prev && prev != t && usable(prev) && me->isEnemy(prev, true)
            && attackersOn(prev, me, now) < s_maxPerTarget && dist2(me, prev) <= s_reassignRange * s_reassignRange)
            pick = prev;
        else
        {
            // 2. the nearest enemy who is less busy (anyone fighting nearby is a candidate)
            Character* best = NULL; float bestScore = 1e30f;
            for (int i = 0; i < s_engN; ++i)
            {
                if (now - s_eng[i].at >= FRESH_MS) continue;
                Character* c = s_eng[i].who;
                if (!c || c == me || c == t || !usable(c) || !me->isEnemy(c, true)) continue;
                float d2 = dist2(me, c);
                if (d2 > s_reassignRange * s_reassignRange) continue;
                int busy = attackersOn(c, me, now);
                if (busy >= s_maxPerTarget) continue;
                float score = d2 + (float)busy * 40000.0f;      // ~20 m of walking per extra attacker
                if (score < bestScore) { bestScore = score; best = c; }
            }
            if (best) pick = best;
        }
    }
    record(me, pick, now);
    LeaveCriticalSection(&s_lock);

    if (pick != t && InterlockedIncrement(&s_redirectLogs) <= 40)
    {
        char b[96]; sprintf_s(b, sizeof(b), "[Warborn] fair_battle: redirected a fighter (cap %d per target)", s_maxPerTarget);
        wbLog(b);
    }
    return pick;
}

// ------------------------------------------------------------ settings + install
static intptr_t resolveExport(const char* mangled)
{
    HMODULE h = GetModuleHandleA("KenshiLib.dll");
    if (!h) return 0;
    unsigned char* stub = (unsigned char*)GetProcAddress(h, mangled);
    if (!stub) return 0;
    if (stub[0] == 0xFF && stub[1] == 0x25)
    {
        int disp = *(int*)(stub + 2);
        void** slot = (void**)(stub + 6 + disp);
        return (intptr_t)*slot;
    }
    return (intptr_t)stub;
}
static bool iniB(const char* path, const char* key, bool d)
{
    char buf[16]; GetPrivateProfileStringA("Warborn", key, d ? "true" : "false", buf, sizeof(buf), path);
    for (char* p = buf; *p; ++p) *p = (char)tolower((unsigned char)*p);
    if (!strcmp(buf, "true") || !strcmp(buf, "1") || !strcmp(buf, "yes") || !strcmp(buf, "on"))  return true;
    if (!strcmp(buf, "false") || !strcmp(buf, "0") || !strcmp(buf, "no") || !strcmp(buf, "off")) return false;
    return d;
}
static float iniF(const char* path, const char* key, float d)
{
    char def[32], buf[64]; sprintf_s(def, sizeof(def), "%g", d);
    GetPrivateProfileStringA("Warborn", key, def, buf, sizeof(buf), path);
    return (float)atof(buf);
}

void wbBattleInstall(const char* path)
{
    InitializeCriticalSection(&s_lock);
    s_fair          = iniB(path, "FairBattles",           s_fair);
    s_maxPerTarget  = (int)iniF(path, "MaxAttackersPerTarget", (float)s_maxPerTarget);
    s_reassignRange = iniF(path, "FairReassignRange",     s_reassignRange);
    s_fairForPlayer = iniB(path, "FairForYourPeople",     s_fairForPlayer);
    if (s_maxPerTarget < 1) s_maxPerTarget = 1;

    // Exported as public by KenshiLib (the header marks it private); try both manglings.
    intptr_t fn = resolveExport("?chooseAttackTarget@CombatClassAI@@QEAAPEAVCharacter@@XZ");
    if (!fn) fn = resolveExport("?chooseAttackTarget@CombatClassAI@@AEAAPEAVCharacter@@XZ");
    if (!fn)
        ErrorLog("Warborn: chooseAttackTarget not found - fair battles off");
    else if (KenshiLib::SUCCESS != KenshiLib::AddHook(fn, (void*)&choose_hook, &s_chooseOrig))
        ErrorLog("Warborn: chooseAttackTarget hook FAILED - fair battles off");
    else
        DebugLog("Warborn: chooseAttackTarget hook OK (fair battles)");
}
