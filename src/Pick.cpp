// Warborn: World - who is under the mouse cursor.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
// Kenshi doesn't "select" strangers you click, so Warborn's talk / recruit
// keys act on the person you POINT AT. Each character's chest is projected
// to the screen (the same method the earlier Direct Control build used in
// game, including Kenshi's root-scene offset) and the nearest one to the
// cursor within PickRadius pixels wins.

#include "Warborn.h"

#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/CameraClass.h>
#include <kenshi/Character.h>
#include <ogre/OgreCamera.h>
#include <ogre/OgreSceneManager.h>
#include <ogre/OgreSceneNode.h>
#include <ogre/OgreMatrix4.h>
#include <ogre/OgreVector4.h>
#include <mygui/MyGUI.h>

static bool project(Ogre::Camera* cam, Ogre::Vector3 gamePos, int& sx, int& sy)
{
    Ogre::Vector3 rootShift = Ogre::Vector3::ZERO;
    if (cam->getSceneManager() && cam->getSceneManager()->getRootSceneNode())
        rootShift = cam->getSceneManager()->getRootSceneNode()->_getDerivedPosition();
    Ogre::Vector3 pt = gamePos + rootShift;
    Ogre::Matrix4 vp = cam->getProjectionMatrix() * cam->getViewMatrix(true);
    Ogre::Vector4 c4 = vp * Ogre::Vector4(pt.x, pt.y, pt.z, 1.0f);
    if (c4.w <= 0.001f) return false;                                     // behind the camera
    MyGUI::IntSize v = MyGUI::RenderManager::getInstance().getViewSize();
    sx = (int)(((c4.x / c4.w) * 0.5f + 0.5f) * v.width);
    sy = (int)((0.5f - (c4.y / c4.w) * 0.5f) * v.height);
    return true;
}

Character* wbPersonUnderMouse(float radiusPx)
{
    if (!ou || !ou->player || !ou->player->camera || !ou->player->camera->camera) return NULL;
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (!im) return NULL;
    MyGUI::IntPoint mp = im->getMousePosition();
    Ogre::Camera* cam = ou->player->camera->camera;
    Character* best = NULL; float bestD2 = radiusPx * radiusPx;
    auto& chars = ou->getCharacterUpdateList();
    for (auto it = chars.begin(); it != chars.end(); ++it)
    {
        Character* c = *it;
        if (!c) continue;
        Ogre::Vector3 p = c->getPosition();
        int sx, sy;
        if (!project(cam, p + Ogre::Vector3(0.0f, 11.0f, 0.0f), sx, sy)) continue;   // about chest height
        float dx = (float)(sx - mp.left), dy = (float)(sy - mp.top);
        float d2 = dx * dx + dy * dy;
        if (c->isPlayerCharacter()) d2 *= 1.5f;                                      // strangers win close calls
        if (d2 < bestD2) { bestD2 = d2; best = c; }
    }
    return best;
}
