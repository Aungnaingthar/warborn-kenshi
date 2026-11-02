// Warborn: World - the world state travels with your save game.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// When Kenshi saves, Warborn writes mods\Warborn\saves\<save name>.txt with
// the news, your renown, grudges, pacts and hunt contracts. When that save
// is loaded again, they come back. One tab-separated record per line.

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/SaveManager.h>
#include <core/Functions.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

static char s_folder[MAX_PATH] = "";

static std::string fileFor(const std::string& save)
{
    std::string clean;
    for (size_t i = 0; i < save.size(); ++i)
    {
        char c = save[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ';
        clean += ok ? c : '_';
    }
    if (clean.empty()) clean = "unnamed";
    return std::string(s_folder) + clean + ".txt";
}

std::vector<std::string> wbSplit(const std::string& line)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;)
    {
        size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return out;
}
std::string wbClean(const std::string& s)                        // no tabs / newlines inside a field
{
    std::string o = s;
    for (size_t i = 0; i < o.size(); ++i) if (o[i] == '\t' || o[i] == '\n' || o[i] == '\r') o[i] = ' ';
    return o;
}

static void writeState(const std::string& save)
{
    if (!s_folder[0] || save.empty()) return;
    CreateDirectoryA(s_folder, NULL);
    std::vector<std::string> lines;
    lines.push_back("warborn\t1");
    wbWorldSave(lines);
    wbPeopleSave(lines);
    wbContractsSave(lines);
    wbUiSave(lines);
    std::string path = fileFor(save);
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) { wbLogAlways("[Warborn] could not write the save file"); return; }
    for (size_t i = 0; i < lines.size(); ++i) { fputs(lines[i].c_str(), f); fputs("\n", f); }
    fclose(f);
    char b[MAX_PATH + 64]; sprintf_s(b, sizeof(b), "[Warborn] world saved (%d records) to %s", (int)lines.size(), path.c_str());
    wbLogAlways(b);
}

void wbPersistLoad()
{
    SaveManager* sm = SaveManager::getSingleton();
    if (!sm || !s_folder[0]) return;
    std::string save = sm->getCurrentGame();
    if (save.empty()) return;
    std::string path = fileFor(save);
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) { wbLog("[Warborn] no saved world for this game yet"); return; }
    std::vector<std::string> lines;
    char buf[2048];
    while (fgets(buf, sizeof(buf), f))
    {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        if (n > 0) lines.push_back(std::string(buf));
    }
    fclose(f);
    wbWorldLoad(lines);
    wbPeopleLoad(lines);
    wbContractsLoad(lines);
    wbUiLoad(lines);
    char b[MAX_PATH + 64]; sprintf_s(b, sizeof(b), "[Warborn] world restored (%d records) from %s", (int)lines.size(), path.c_str());
    wbLogAlways(b);
}

static void (*s_saveOrig)(SaveManager*, const std::string&, bool) = NULL;
static void save_hook(SaveManager* self, const std::string& name, bool autosave)
{
    s_saveOrig(self, name, autosave);
    writeState(name);
}

void wbPersistInstall(const char* modFolder)
{
    strcpy_s(s_folder, sizeof(s_folder), modFolder);
    strcat_s(s_folder, sizeof(s_folder), "saves\\");
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(&SaveManager::save), (void*)&save_hook, &s_saveOrig))
        ErrorLog("Warborn: save hook FAILED - the world state won't be saved");
    else
        DebugLog("Warborn: save hook OK");
}
