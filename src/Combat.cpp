// Warborn: World - realistic combat rules for everyone (no direct control).
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
//   * REALISTIC SPEED: vanilla Kenshi lets high skill swing and block
//     superhumanly fast. The game's attack/block speed is capped, so a master
//     is never faster than a human can be.
//   * SKILL = PRECISION, NOT SPEED: the skill gap between attacker and
//     defender scales damage instead, and a skilled fighter finds the gaps in
//     light armour - a master can drop an unarmoured man in one or two blows,
//     two armoured equals have a long, hard fight.
// Applies to every fighter in the world, NPCs included. All values in [Warborn].

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Damages.h>
#include <core/Functions.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static bool  s_speedCap       = true;
static float s_attackSpeedMax = 1.2f;
static float s_blockSpeedMax  = 1.2f;
static bool  s_skillDamage    = true;
static float s_gapScale       = 0.6f;
static float s_skillMultMin   = 0.6f;
static float s_skillMultMax   = 1.6f;
static float s_unarmored      = 0.8f;
static float s_piecesForFull  = 4.0f;
static float s_totalMin       = 0.35f;
static float s_totalMax       = 2.2f;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ------------------------------------------------------------ damage
void wbCombatBeforeHit(Character* victim, Character* attacker, Damages& damage)
{
    if (!s_skillDamage || !victim || !attacker || victim == attacker) return;
    CharStats* as = attacker->getStats();
    CharStats* vs = victim->getStats();
    if (!as || !vs) return;
    float atk = clampf(as->getEquippedWeaponSkill(), 0.0f, 120.0f);
    float def = clampf(vs->getMeleeDefence(false),   0.0f, 120.0f);
    float skillMult = clampf(1.0f + ((atk - def) / 100.0f) * s_gapScale, s_skillMultMin, s_skillMultMax);
    MedicalSystem* med = victim->getMedical();
    float pieces   = med ? (float)med->armourList.size() : s_piecesForFull;
    float exposure = clampf(1.0f - pieces / s_piecesForFull, 0.0f, 1.0f);
    float exposeMult = 1.0f + s_unarmored * exposure * (atk / 100.0f);
    float total = clampf(skillMult * exposeMult, s_totalMin, s_totalMax);
    damage.cut    *= total;
    damage.blunt  *= total;
    damage.pierce *= total;
}

// ------------------------------------------------------------ speed
static float (*s_speedOrig)(CharStats*, float, float, bool) = NULL;
static volatile LONG s_speedLogs = 0;
static volatile bool s_speedOdd  = false;
static float speed_hook(CharStats* st, float weightMult, float skill, bool isBlock)
{
    float v = s_speedOrig(st, weightMult, skill, isBlock);
    if (v > 5.0f && !s_speedOdd) { s_speedOdd = true; DebugLog("[Warborn] speed scale unexpected - speed cap switched off"); }
    float out = v;
    if (s_speedCap && !s_speedOdd)
    {
        float cap = isBlock ? s_blockSpeedMax : s_attackSpeedMax;
        if (out > cap) out = cap;
    }
    if (InterlockedIncrement(&s_speedLogs) <= 30)
    {
        char b[128];
        sprintf_s(b, sizeof(b), "[Warborn] speed %s skill=%.0f weight=%.2f raw=%.2f used=%.2f",
                  isBlock ? "block" : "attack", skill, weightMult, v, out);
        DebugLog(b);
    }
    return out;
}

// KenshiLib exports are `jmp [rip+slot]` stubs whose slot holds the real game
// address for the running exe - resolve it so protected members can be hooked.
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

// ------------------------------------------------------------ settings + install
static float iniF(const char* path, const char* key, float d)
{
    char def[32], buf[64]; sprintf_s(def, sizeof(def), "%g", d);
    GetPrivateProfileStringA("Warborn", key, def, buf, sizeof(buf), path);
    return (float)atof(buf);
}
static bool iniB(const char* path, const char* key, bool d)
{
    char buf[16]; GetPrivateProfileStringA("Warborn", key, d ? "true" : "false", buf, sizeof(buf), path);
    for (char* p = buf; *p; ++p) *p = (char)tolower((unsigned char)*p);
    if (!strcmp(buf, "true") || !strcmp(buf, "1") || !strcmp(buf, "yes") || !strcmp(buf, "on"))  return true;
    if (!strcmp(buf, "false") || !strcmp(buf, "0") || !strcmp(buf, "no") || !strcmp(buf, "off")) return false;
    return d;
}

void wbCombatInstall(const char* path)
{
    s_speedCap       = iniB(path, "RealisticSpeed",     s_speedCap);
    s_attackSpeedMax = iniF(path, "AttackSpeedMax",     s_attackSpeedMax);
    s_blockSpeedMax  = iniF(path, "BlockSpeedMax",      s_blockSpeedMax);
    s_skillDamage    = iniB(path, "SkillDamage",        s_skillDamage);
    s_gapScale       = iniF(path, "SkillGapScale",      s_gapScale);
    s_skillMultMin   = iniF(path, "SkillMultMin",       s_skillMultMin);
    s_skillMultMax   = iniF(path, "SkillMultMax",       s_skillMultMax);
    s_unarmored      = iniF(path, "UnarmoredBonus",     s_unarmored);
    s_piecesForFull  = iniF(path, "ArmorPiecesForFull", s_piecesForFull);
    s_totalMin       = iniF(path, "MinTotalMult",       s_totalMin);
    s_totalMax       = iniF(path, "MaxTotalMult",       s_totalMax);
    if (s_piecesForFull < 1.0f) s_piecesForFull = 1.0f;
    if (s_skillMultMin > s_skillMultMax) s_skillMultMin = s_skillMultMax;
    if (s_totalMin > s_totalMax) s_totalMin = s_totalMax;

    // KenshiLib may export it as protected, public or private: try all three manglings.
    intptr_t sp = resolveExport("?calculateAttackOrBlockSpeed@CharStats@@IEAAMMM_N@Z");
    if (!sp) sp = resolveExport("?calculateAttackOrBlockSpeed@CharStats@@QEAAMMM_N@Z");
    if (!sp) sp = resolveExport("?calculateAttackOrBlockSpeed@CharStats@@AEAAMMM_N@Z");
    if (!sp)
        ErrorLog("Warborn: calculateAttackOrBlockSpeed not found - speed cap off");
    else if (KenshiLib::SUCCESS != KenshiLib::AddHook(sp, (void*)&speed_hook, &s_speedOrig))
        ErrorLog("Warborn: speed hook FAILED - speed cap off");
    else
        DebugLog("Warborn: speed hook OK (realistic speed)");
}
