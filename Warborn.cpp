// Warborn: KCD 2-style combat layer for Kenshi.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// Phase 1
//   * Skill + armour based melee damage, for EVERY fighter (NPCs too, so it
//     stays fair): a big skill gap hits harder, an unarmoured target takes much
//     more from a skilled attacker (a master can drop a naked enemy in 1-2
//     hits), equal armoured fighters stay long and tense.
//   * Impact feel when the player is in the exchange: a tiny hit-stop and a
//     camera jolt, scaled by how hard the hit was and what it hit (flesh,
//     armour, a block).
//
// NOTE: must compile with the VS2010 (v100) compiler: no range-for, no
// initializer lists, no std::min/max (Windows.h macros) - use the helpers below.

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Damages.h>
#include <ogre/OgreCamera.h>
#include <ogre/OgreVector3.h>

#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

// ---------------------------------------------------------------- settings

struct WarbornConfig
{
    bool  enabled;
    bool  log;

    // damage
    bool  skillDamage;
    float globalDamageMult;     // everything, last
    float skillGapScale;        // +-(skill gap / 100) * this
    float skillMultMin;
    float skillMultMax;
    float unarmoredBonus;       // extra damage vs a fully exposed target, at attacker skill 100
    float armorPiecesForFull;   // this many worn armour pieces = no exposure bonus
    float maxTotalMult;
    float minTotalMult;

    // impact
    bool  hitStop;
    float hitStopMs;            // at a "reference" hit
    float hitStopMaxMs;
    float hitStopSpeed;         // game speed during the stop (fraction of normal)
    bool  cameraShake;
    float shakeAmp;             // world units (~10 cm) at a reference hit
    float shakeMs;
    float impactRefDamage;      // Damages::total() that counts as a "reference" hit
    float blockImpact;          // multiplier when the hit was blocked
    float armorImpact;          // multiplier when it hit metal / chain
    float takenImpact;          // multiplier when the PLAYER is the one hit
};

static WarbornConfig g_cfg;

static void setDefaults()
{
    g_cfg.enabled            = true;
    g_cfg.log                = false;

    g_cfg.skillDamage        = true;
    g_cfg.globalDamageMult   = 1.0f;
    g_cfg.skillGapScale      = 0.6f;
    g_cfg.skillMultMin       = 0.6f;
    g_cfg.skillMultMax       = 1.6f;
    g_cfg.unarmoredBonus     = 0.8f;
    g_cfg.armorPiecesForFull = 4.0f;
    g_cfg.maxTotalMult       = 2.2f;
    g_cfg.minTotalMult       = 0.35f;

    g_cfg.hitStop            = true;
    g_cfg.hitStopMs          = 70.0f;
    g_cfg.hitStopMaxMs       = 140.0f;
    g_cfg.hitStopSpeed       = 0.08f;
    g_cfg.cameraShake        = true;
    g_cfg.shakeAmp           = 0.8f;
    g_cfg.shakeMs            = 200.0f;
    g_cfg.impactRefDamage    = 40.0f;
    g_cfg.blockImpact        = 0.5f;
    g_cfg.armorImpact        = 0.75f;
    g_cfg.takenImpact        = 0.85f;
}

static float wbClamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static float iniFloat(const char* path, const char* key, float dflt)
{
    char def[32], buf[64];
    sprintf_s(def, sizeof(def), "%g", dflt);
    GetPrivateProfileStringA("Warborn", key, def, buf, sizeof(buf), path);
    return (float)atof(buf);
}

static bool iniBool(const char* path, const char* key, bool dflt)
{
    char buf[16];
    GetPrivateProfileStringA("Warborn", key, dflt ? "true" : "false", buf, sizeof(buf), path);
    for (char* p = buf; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    if (!strcmp(buf, "true") || !strcmp(buf, "1") || !strcmp(buf, "yes") || !strcmp(buf, "on"))  return true;
    if (!strcmp(buf, "false") || !strcmp(buf, "0") || !strcmp(buf, "no") || !strcmp(buf, "off")) return false;
    return dflt;
}

void warbornLoadConfig(const char* path)
{
    setDefaults();
    if (!path || !path[0]) return;

    g_cfg.enabled            = iniBool (path, "Enabled",            g_cfg.enabled);
    g_cfg.log                = iniBool (path, "Log",                g_cfg.log);

    g_cfg.skillDamage        = iniBool (path, "SkillDamage",        g_cfg.skillDamage);
    g_cfg.globalDamageMult   = iniFloat(path, "GlobalDamageMult",   g_cfg.globalDamageMult);
    g_cfg.skillGapScale      = iniFloat(path, "SkillGapScale",      g_cfg.skillGapScale);
    g_cfg.skillMultMin       = iniFloat(path, "SkillMultMin",       g_cfg.skillMultMin);
    g_cfg.skillMultMax       = iniFloat(path, "SkillMultMax",       g_cfg.skillMultMax);
    g_cfg.unarmoredBonus     = iniFloat(path, "UnarmoredBonus",     g_cfg.unarmoredBonus);
    g_cfg.armorPiecesForFull = iniFloat(path, "ArmorPiecesForFull", g_cfg.armorPiecesForFull);
    g_cfg.maxTotalMult       = iniFloat(path, "MaxTotalMult",       g_cfg.maxTotalMult);
    g_cfg.minTotalMult       = iniFloat(path, "MinTotalMult",       g_cfg.minTotalMult);

    g_cfg.hitStop            = iniBool (path, "HitStop",            g_cfg.hitStop);
    g_cfg.hitStopMs          = iniFloat(path, "HitStopMs",          g_cfg.hitStopMs);
    g_cfg.hitStopMaxMs       = iniFloat(path, "HitStopMaxMs",       g_cfg.hitStopMaxMs);
    g_cfg.hitStopSpeed       = iniFloat(path, "HitStopSpeed",       g_cfg.hitStopSpeed);
    g_cfg.cameraShake        = iniBool (path, "CameraShake",        g_cfg.cameraShake);
    g_cfg.shakeAmp           = iniFloat(path, "ShakeAmp",           g_cfg.shakeAmp);
    g_cfg.shakeMs            = iniFloat(path, "ShakeMs",            g_cfg.shakeMs);
    g_cfg.impactRefDamage    = iniFloat(path, "ImpactRefDamage",    g_cfg.impactRefDamage);
    g_cfg.blockImpact        = iniFloat(path, "BlockImpact",        g_cfg.blockImpact);
    g_cfg.armorImpact        = iniFloat(path, "ArmorImpact",        g_cfg.armorImpact);
    g_cfg.takenImpact        = iniFloat(path, "TakenImpact",        g_cfg.takenImpact);

    // sanity
    if (g_cfg.skillMultMin > g_cfg.skillMultMax) g_cfg.skillMultMin = g_cfg.skillMultMax;
    if (g_cfg.minTotalMult > g_cfg.maxTotalMult) g_cfg.minTotalMult = g_cfg.maxTotalMult;
    if (g_cfg.armorPiecesForFull < 1.0f) g_cfg.armorPiecesForFull = 1.0f;
    if (g_cfg.impactRefDamage < 1.0f)    g_cfg.impactRefDamage = 1.0f;
    g_cfg.hitStopSpeed = wbClamp(g_cfg.hitStopSpeed, 0.01f, 1.0f);
    g_cfg.hitStopMaxMs = wbClamp(g_cfg.hitStopMaxMs, 0.0f, 400.0f);
    g_cfg.shakeMs      = wbClamp(g_cfg.shakeMs, 1.0f, 1000.0f);

    char b[256];
    sprintf_s(b, sizeof(b),
        "[Warborn] config enabled=%d skillDamage=%d gapScale=%.2f unarmored=%.2f maxMult=%.2f hitStop=%d(%.0fms) shake=%d(%.2f)",
        g_cfg.enabled ? 1 : 0, g_cfg.skillDamage ? 1 : 0, g_cfg.skillGapScale, g_cfg.unarmoredBonus,
        g_cfg.maxTotalMult, g_cfg.hitStop ? 1 : 0, g_cfg.hitStopMs, g_cfg.cameraShake ? 1 : 0, g_cfg.shakeAmp);
    DebugLog(b);
}

// ------------------------------------------------------------------ damage

// Carries the scaled hit from warbornBeforeHit to warbornAfterHit.  Both run
// inside the same game call (the host wraps the hook), so one slot is enough.
static float      s_lastHitTotal  = 0.0f;
static Character* s_lastHitVictim = nullptr;

void warbornBeforeHit(Character* victim, Character* attacker, CutDirection dir,
                      Damages& damage, Character* player)
{
    s_lastHitTotal  = 0.0f;
    s_lastHitVictim = victim;
    if (!g_cfg.enabled || !victim || !attacker || victim == attacker) return;

    if (g_cfg.skillDamage)
    {
        CharStats* as = attacker->getStats();
        CharStats* vs = victim->getStats();
        if (as && vs)
        {
            float atk = wbClamp(as->getEquippedWeaponSkill(), 0.0f, 120.0f);
            float def = wbClamp(vs->getMeleeDefence(false),   0.0f, 120.0f);

            // 1) skill gap: a master vs a novice hits harder, a novice vs a master weaker
            float skillMult = 1.0f + ((atk - def) / 100.0f) * g_cfg.skillGapScale;
            skillMult = wbClamp(skillMult, g_cfg.skillMultMin, g_cfg.skillMultMax);

            // 2) exposure: few armour pieces = a skilled attacker finds flesh
            MedicalSystem* med = victim->getMedical();
            float pieces   = med ? (float)med->armourList.size() : g_cfg.armorPiecesForFull;
            float exposure = wbClamp(1.0f - pieces / g_cfg.armorPiecesForFull, 0.0f, 1.0f);
            float exposeMult = 1.0f + g_cfg.unarmoredBonus * exposure * (atk / 100.0f);

            float total = skillMult * exposeMult * g_cfg.globalDamageMult;
            total = wbClamp(total, g_cfg.minTotalMult, g_cfg.maxTotalMult);

            float before = damage.total();
            damage.cut    *= total;
            damage.blunt  *= total;
            damage.pierce *= total;

            if (g_cfg.log && player && (victim == player || attacker == player))
            {
                char b[256];
                sprintf_s(b, sizeof(b),
                    "[Warborn] hit %s atk=%.0f def=%.0f armour=%.0f skillMult=%.2f exposeMult=%.2f total=%.2f dmg %.1f -> %.1f dir=%d",
                    victim == player ? "ON_PLAYER" : "BY_PLAYER", atk, def, pieces,
                    skillMult, exposeMult, total, before, damage.total(), (int)dir);
                DebugLog(b);
            }
        }
    }
    s_lastHitTotal = damage.total();
}

// ------------------------------------------------------------------ impact

static ULONGLONG s_stopPendingMs   = 0;      // queued by a hit, started next frame
static bool      s_stopActive      = false;
static ULONGLONG s_stopUntil       = 0;
static float     s_stopSavedMult   = 1.0f;
static float     s_stopAppliedMult = 1.0f;
static bool      s_stopFirstLogged = false;

static ULONGLONG     s_shakeStart  = 0;
static float         s_shakeDurMs  = 0.0f;
static float         s_shakeAmp    = 0.0f;
static Ogre::Camera* s_shakeCam    = nullptr;
static Ogre::Vector3 s_shakeLastOffset(0.0f, 0.0f, 0.0f);
static Ogre::Vector3 s_shakeLastPos(0.0f, 0.0f, 0.0f);
static bool          s_shakeHaveLast = false;

void warbornAfterHit(Character* victim, Character* attacker, HitMaterialType result,
                     Character* player)
{
    if (!g_cfg.enabled || !player || victim != s_lastHitVictim) return;
    if (victim != player && attacker != player) return;
    if (result == HIT_MISSED) return;

    float k = wbClamp(s_lastHitTotal / g_cfg.impactRefDamage, 0.3f, 1.6f);
    if (result == HIT_SWORD)                            k *= g_cfg.blockImpact;   // blocked: a clang, not a crunch
    else if (result == HIT_METAL || result == HIT_CHAIN) k *= g_cfg.armorImpact;  // armour soaked it
    if (victim == player)                               k *= g_cfg.takenImpact;

    if (g_cfg.hitStop)
    {
        ULONGLONG ms = (ULONGLONG)wbClamp(g_cfg.hitStopMs * k, 0.0f, g_cfg.hitStopMaxMs);
        if (ms > s_stopPendingMs) s_stopPendingMs = ms;
    }
    if (g_cfg.cameraShake)
    {
        ULONGLONG now = GetTickCount64();
        // a new, stronger hit restarts the jolt; a weaker one mid-shake is absorbed
        float remaining = 0.0f;
        if (s_shakeDurMs > 0.0f && now < s_shakeStart + (ULONGLONG)s_shakeDurMs)
        {
            float t = (float)(now - s_shakeStart) / s_shakeDurMs;
            remaining = s_shakeAmp * (1.0f - t) * (1.0f - t);
        }
        float amp = g_cfg.shakeAmp * k;
        if (amp > remaining)
        {
            s_shakeStart = now;
            s_shakeDurMs = g_cfg.shakeMs;
            s_shakeAmp   = amp;
        }
    }
}

static void stopRestore(GameWorld* world, const char* why)
{
    if (!s_stopActive) return;
    s_stopActive = false;
    if (!world) return;
    // Only put the speed back if nobody (the player, the game) changed it meanwhile.
    float now = world->getFrameSpeedMultiplier();
    if (fabsf(now - s_stopAppliedMult) < 0.0001f)
        world->setFrameSpeedMultiplier(s_stopSavedMult);
    if (g_cfg.log)
    {
        char b[128];
        sprintf_s(b, sizeof(b), "[Warborn] hitstop_end why=%s restored=%.3f found=%.3f", why, s_stopSavedMult, now);
        DebugLog(b);
    }
}

void warbornFrame(GameWorld* world)
{
    if (!world) return;
    if (!g_cfg.enabled || !g_cfg.hitStop || world->isPaused() || world->isLoadingFromASaveGame())
    {
        stopRestore(world, "paused_or_off");
        s_stopPendingMs = 0;
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (s_stopActive && now >= s_stopUntil) stopRestore(world, "done");

    if (s_stopPendingMs > 0 && !s_stopActive)
    {
        float cur = world->getFrameSpeedMultiplier();
        if (!s_stopFirstLogged)
        {
            // first-run check: tells us what this value really holds on the player's exe
            char b[96];
            sprintf_s(b, sizeof(b), "[Warborn] hitstop_first frameSpeedMult=%.3f", cur);
            DebugLog(b);
            s_stopFirstLogged = true;
        }
        if (cur > 0.001f)
        {
            s_stopSavedMult   = cur;
            s_stopAppliedMult = cur * g_cfg.hitStopSpeed;
            world->setFrameSpeedMultiplier(s_stopAppliedMult);
            s_stopActive = true;
            s_stopUntil  = now + s_stopPendingMs;
        }
    }
    s_stopPendingMs = 0;   // hits during an active stop don't chain into a freeze
}

void warbornCamera(Ogre::Camera* cam)
{
    if (!cam) return;
    if (cam != s_shakeCam) { s_shakeCam = cam; s_shakeHaveLast = false; }

    // Undo last frame's jolt if nothing moved the camera since (if the game
    // re-placed it this frame, the old offset is already gone).
    if (s_shakeHaveLast && cam->getPosition() == s_shakeLastPos)
        cam->move(-s_shakeLastOffset);
    s_shakeHaveLast = false;

    if (!g_cfg.enabled || !g_cfg.cameraShake || s_shakeDurMs <= 0.0f) return;
    ULONGLONG now = GetTickCount64();
    if (now >= s_shakeStart + (ULONGLONG)s_shakeDurMs) { s_shakeDurMs = 0.0f; return; }

    float t     = (float)(now - s_shakeStart) / s_shakeDurMs;
    float decay = (1.0f - t) * (1.0f - t);
    float a     = s_shakeAmp * decay;
    float ms    = (float)(now - s_shakeStart);
    Ogre::Vector3 off(sinf(ms * 0.090f)        * a,
                      sinf(ms * 0.130f + 1.3f) * a * 0.7f,
                      sinf(ms * 0.110f + 2.1f) * a * 0.5f);
    cam->move(off);
    s_shakeLastOffset = off;
    s_shakeLastPos    = cam->getPosition();
    s_shakeHaveLast   = true;
}

void warbornReset(GameWorld* world)
{
    stopRestore(world, "reset");
    s_stopPendingMs = 0;
    s_shakeDurMs    = 0.0f;
    s_shakeHaveLast = false;
    s_shakeCam      = nullptr;
    s_lastHitTotal  = 0.0f;
    s_lastHitVictim = nullptr;
}
