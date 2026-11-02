// Warborn: World - the player's purse.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
// Kept in its own file: Platoon.h (Ownerships) and Building.h both define
// BuildingDesignation, so this is the only file that includes Platoon.h.

#include "Warborn.h"

#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Faction.h>
#include <kenshi/Platoon.h>

static Ownerships* purse()
{
    Faction* pf = wbPlayerFaction();
    return pf ? pf->factionOwnerships : NULL;
}
int wbPlayerMoney()
{
    Ownerships* o = purse();
    return o ? o->getMoney() : 0;
}
void wbPayPlayer(int cats)
{
    Ownerships* o = purse();
    if (o && cats > 0) o->addMoney(cats);
}
bool wbChargePlayer(int cats)
{
    Ownerships* o = purse();
    if (!o || cats <= 0 || o->getMoney() < cats) return false;
    return o->takeMoney(cats);
}
void wbTakeMoneyByForce(int cats)
{
    Ownerships* o = purse();
    if (o && cats > 0) o->takeMoneyByForce(cats);
}
