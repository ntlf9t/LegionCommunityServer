/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "adtfile.h"
#include "Banner.h"
#include "Common.h"
#include "cascfile.h"
#include "DB2CascFileSource.h"
#include "ExtractorDB2LoadInfo.h"
#include "StringFormat.h"
#include "VMapDefinitions.h"
#include "vmapexport.h"
#include "wdtfile.h"
#include <CascLib.h>
#include <boost/filesystem/operations.hpp>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <cstdio>
#include <cerrno>

#ifdef _WIN32
    #include <direct.h>
    #define mkdir _mkdir
#endif

//-----------------------------------------------------------------------------

CASC::StorageHandle CascStorage;

struct map_info
{
    char name[64];
    int32 parent_id;
};

std::map<uint32, map_info> map_ids;
std::unordered_set<uint32> maps_that_are_parents;
boost::filesystem::path input_path;
bool preciseVectorData = false;
std::unordered_map<std::string, WMODoodadData> WmoDoodads;

// Constants

const char* szWorkDirWmo = "./Buildings";

#define CASC_LOCALES_COUNT 17
char const* CascLocaleNames[CASC_LOCALES_COUNT] =
{
    "none", "enUS",
    "koKR", "unknown",
    "frFR", "deDE",
    "zhCN", "esES",
    "zhTW", "enGB",
    "enCN", "enTW",
    "esMX", "ruRU",
    "ptBR", "itIT",
    "ptPT"
};

uint32 WowLocaleToCascLocaleFlags[12] =
{
    CASC_LOCALE_ENUS | CASC_LOCALE_ENGB,
    CASC_LOCALE_KOKR,
    CASC_LOCALE_FRFR,
    CASC_LOCALE_DEDE,
    CASC_LOCALE_ZHCN,
    CASC_LOCALE_ZHTW,
    CASC_LOCALE_ESES,
    CASC_LOCALE_ESMX,
    CASC_LOCALE_RURU,
    0,
    CASC_LOCALE_PTBR | CASC_LOCALE_PTPT,
    CASC_LOCALE_ITIT,
};

bool OpenCascStorage(int locale)
{
    try
    {
        boost::filesystem::path const storage_dir(boost::filesystem::canonical(input_path) / "Data");
        CascStorage = CASC::OpenStorage(storage_dir, WowLocaleToCascLocaleFlags[locale]);
        if (!CascStorage)
        {
            printf("error opening casc storage '%s' locale %s\n", storage_dir.string().c_str(), localeNames[locale]);
            return false;
        }

        return true;
    }
    catch (boost::filesystem::filesystem_error const& error)
    {
        printf("error opening casc storage : %s\n", error.what());
        return false;
    }
}

uint32 GetInstalledLocalesMask()
{
    try
    {
        boost::filesystem::path const storage_dir(boost::filesystem::canonical(input_path) / "Data");
        CASC::StorageHandle storage = CASC::OpenStorage(storage_dir, 0);
        if (!storage)
            return false;

        return CASC::GetInstalledLocalesMask(storage);
    }
    catch (boost::filesystem::filesystem_error const& error)
    {
        printf("Unable to determine installed locales mask: %s\n", error.what());
    }

    return 0;
}

std::map<std::pair<uint32, uint16>, uint32> uniqueObjectIds;

uint32 GenerateUniqueObjectId(uint32 clientId, uint16 clientDoodadId)
{
    return uniqueObjectIds.emplace(std::make_pair(clientId, clientDoodadId), uniqueObjectIds.size() + 1).first->second;
}

// Local testing functions
bool FileExists(const char* file)
{
    if (FILE* n = fopen(file, "rb"))
    {
        fclose(n);
        return true;
    }
    return false;
}

bool ExtractSingleWmo(std::string& fname)
{
    // Copy files from archive
    std::string originalName = fname;

    char szLocalFile[1024];
    char* plain_name = GetPlainName(&fname[0]);
    FixNameCase(plain_name, strlen(plain_name));
    FixNameSpaces(plain_name, strlen(plain_name));
    sprintf(szLocalFile, "%s/%s", szWorkDirWmo, plain_name);

    if (FileExists(szLocalFile))
        return true;

    int p = 0;
    // Select root wmo files
    char const* rchr = strrchr(plain_name, '_');
    if (rchr != nullptr)
    {
        char cpy[4];
        memcpy(cpy, rchr, 4);
        for (int i = 0; i < 4; ++i)
        {
            int m = cpy[i];
            if (isdigit(m))
                p++;
        }
    }

    if (p == 3)
        return true;

    bool file_ok = true;
    printf("Extracting %s\n", originalName.c_str());
    WMORoot froot(originalName);
    if (!froot.open())
    {
        printf("Couldn't open RootWmo!!!\n");
        return true;
    }
    FILE *output = fopen(szLocalFile,"wb");
    if(!output)
    {
        printf("couldn't open %s for writing!\n", szLocalFile);
        return false;
    }
    froot.ConvertToVMAPRootWmo(output);
    WMODoodadData& doodads = WmoDoodads[plain_name];
    std::swap(doodads, froot.DoodadData);
    int Wmo_nVertices = 0;
    //printf("root has %d groups\n", froot->nGroups);
    for (std::size_t i = 0; i < froot.groupFileDataIDs.size(); ++i)
    {
        std::string s = Trinity::StringFormat("FILE{:08X}.xxx", froot.groupFileDataIDs[i]);
        WMOGroup fgroup(s);
        if (!fgroup.open(&froot))
        {
            printf("Could not open all Group file for: %s\n", plain_name);
            file_ok = false;
            break;
        }

        Wmo_nVertices += fgroup.ConvertToVMAPGroupWmo(output, preciseVectorData);
        for (uint16 groupReference : fgroup.DoodadReferences)
        {
            if (groupReference >= doodads.Spawns.size())
                continue;

            uint32 doodadNameIndex = doodads.Spawns[groupReference].NameIndex;
            if (froot.ValidDoodadNames.find(doodadNameIndex) == froot.ValidDoodadNames.end())
                continue;

            doodads.References.insert(groupReference);
        }
    }

    fseek(output, 8, SEEK_SET); // store the correct no of vertices
    fwrite(&Wmo_nVertices,sizeof(int),1,output);
    fclose(output);

    // Delete the extracted file in the case of an error
    if (!file_ok)
        remove(szLocalFile);
    return true;
}

void ParsMapFiles()
{
    std::unordered_map<uint32, WDTFile> wdts;
    auto getWDT = [&wdts](uint32 mapId) -> WDTFile*
    {
        auto itr = wdts.find(mapId);
        if (itr == wdts.end())
        {
            char fn[512];
            char* name = map_ids[mapId].name;
            sprintf(fn, "World\\Maps\\%s\\%s.wdt", name, name);
            itr = wdts.emplace(std::piecewise_construct, std::forward_as_tuple(mapId), std::forward_as_tuple(fn, name, maps_that_are_parents.count(mapId) > 0)).first;
            if (!itr->second.init(mapId))
            {
                wdts.erase(itr);
                return nullptr;
            }
        }

        return &itr->second;
    };

    for (auto itr = map_ids.begin(); itr != map_ids.end(); ++itr)
    {
        if (WDTFile* WDT = getWDT(itr->first))
        {
            WDTFile* parentWDT = itr->second.parent_id >= 0 ? getWDT(itr->second.parent_id) : nullptr;
            printf("Processing Map %u\n[", itr->first);
            for (int32 x = 0; x < 64; ++x)
            {
                for (int32 y = 0; y < 64; ++y)
                {
                    bool success = false;
                    if (ADTFile* ADT = WDT->GetMap(x, y))
                    {
                        success = ADT->init(itr->first, itr->first);
                        WDT->FreeADT(ADT);
                    }
                    if (!success && parentWDT)
                    {
                        if (ADTFile* ADT = parentWDT->GetMap(x, y))
                        {
                            ADT->init(itr->first, itr->second.parent_id);
                            parentWDT->FreeADT(ADT);
                        }
                    }
                }
                printf("#");
                fflush(stdout);
            }
            printf("]\n");
        }
    }
}

bool processArgv(int argc, char ** argv, const char *versionString)
{
    bool result = true;
    preciseVectorData = false;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp("-s", argv[i]) == 0)
        {
            preciseVectorData = false;
        }
        else if (strcmp("-d", argv[i]) == 0)
        {
            if ((i + 1) < argc)
            {
                input_path = boost::filesystem::path(argv[i + 1]);
                ++i;
            }
            else
            {
                result = false;
            }
        }
        else if (strcmp("-?", argv[1]) == 0)
        {
            result = false;
        }
        else if(strcmp("-l",argv[i]) == 0)
        {
            preciseVectorData = true;
        }
        else
        {
            result = false;
            break;
        }
    }

    if (!result)
    {
        printf("Extract %s.\n",versionString);
        printf("%s [-?][-s][-l][-d <path>]\n", argv[0]);
        printf("   -s : (default) small size (data size optimization), ~500MB less vmap data.\n");
        printf("   -l : large size, ~500MB more vmap data. (might contain more details)\n");
        printf("   -d <path>: Path to the vector data source folder.\n");
        printf("   -? : This message.\n");
    }

    return result;
}

static bool RetardCheck()
{
    try
    {
        boost::filesystem::path storageDir(boost::filesystem::canonical(input_path) / "Data");
        boost::filesystem::directory_iterator end;
        for (boost::filesystem::directory_iterator itr(storageDir); itr != end; ++itr)
        {
            if (itr->path().extension() == ".MPQ")
            {
                printf("MPQ files found in Data directory!\n");
                printf("This tool works only with World of Warcraft: Legion\n");
                printf("\n");
                printf("To extract maps for Wrath of the Lich King, rebuild tools using 3.3.5 branch!\n");
                printf("\n");
                printf("Press ENTER to exit...\n");
                getchar();
                return false;
            }
        }
    }
    catch (std::exception const& error)
    {
        printf("Error checking client version: %s\n", error.what());
    }

    return true;
}

int main(int argc, char ** argv)
{
    Trinity::Banner::Show("VMAP data extractor", [](char const* text) { printf("%s\n", text); }, nullptr);

    bool success = true;

    // Use command line arguments, when some
    if (!processArgv(argc, argv, VMAP::VMAP_MAGIC))
        return 1;

    if (!RetardCheck())
        return 1;

    // some simple check if working dir is dirty
    else
    {
        std::string sdir = std::string(szWorkDirWmo) + "/dir";
        std::string sdir_bin = std::string(szWorkDirWmo) + "/dir_bin";
        struct stat status;
        if (!stat(sdir.c_str(), &status) || !stat(sdir_bin.c_str(), &status))
        {
            printf("Your output directory seems to be polluted, please use an empty directory!\n");
            printf("<press return to exit>");
            char garbage[2];
            return scanf("%c", garbage);
        }
    }

    printf("Extract %s. Beginning work ....\n", VMAP::VMAP_MAGIC);
    //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    // Create the working directory
    if (mkdir(szWorkDirWmo
#if defined(__linux__) || defined(__APPLE__) || defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
                    , 0711
#endif
                    ))
            success = (errno == EEXIST);

    uint32 installedLocalesMask = GetInstalledLocalesMask();
    int32 FirstLocale = -1;
    for (int i = 0; i < TOTAL_LOCALES; ++i)
    {
        if (i == LOCALE_none)
            continue;

        if (!(installedLocalesMask & WowLocaleToCascLocaleFlags[i]))
            continue;

        if (!OpenCascStorage(i))
            continue;

        FirstLocale = i;
        uint32 build = CASC::GetBuildNumber(CascStorage);
        if (!build)
        {
            CascStorage.reset();
            continue;
        }

        printf("Detected client build %u for locale %s\n\n", build, localeNames[i]);
        break;
    }

    if (FirstLocale == -1)
    {
        printf("FATAL ERROR: No locales defined, unable to continue.\n");
        return 1;
    }

    // Extract models, listed in GameObjectDisplayInfo.dbc
    ExtractGameobjectModels();

    //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    //map.dbc
    if (success)
    {
        printf("Read Map.dbc file... ");

        DB2CascFileSource source(CascStorage, "DBFilesClient\\Map.db2");
        DB2FileLoader db2;
        try
        {
            db2.Load(&source, MapLoadInfo::Instance());
        }
        catch (std::exception const& e)
        {
            printf("Fatal error: Invalid Map.db2 file format! %s\n%s\n",  CASC::HumanReadableCASCError(GetLastError()), e.what());
            exit(1);
        }

        for (uint32 x = 0; x < db2.GetRecordCount(); ++x)
        {
            DB2Record record = db2.GetRecord(x);
            map_info& m = map_ids[record.GetId()];

            const char* map_name = record.GetString("Directory");
            size_t max_map_name_length = sizeof(m.name);
            if (strlen(map_name) >= max_map_name_length)
            {
                printf("Fatal error: Map name too long!\n");
                exit(1);
            }

            strncpy(m.name, map_name, max_map_name_length);
            m.name[max_map_name_length - 1] = '\0';
            m.parent_id = int16(record.GetUInt16("ParentMapID"));
            if (m.parent_id < 0)
                m.parent_id = int16(record.GetUInt16("CosmeticParentMapID"));

            if (m.parent_id >= 0)
                maps_that_are_parents.insert(m.parent_id);
        }

        for (uint32 x = 0; x < db2.GetRecordCopyCount(); ++x)
        {
            DB2RecordCopy copy = db2.GetRecordCopy(x);
            auto itr = map_ids.find(copy.SourceRowId);
            if (itr != map_ids.end())
            {
                map_info& id = map_ids[copy.NewRowId];
                strcpy(id.name, itr->second.name);
                id.parent_id = itr->second.parent_id;
            }
        }

        printf("Done! (" SZFMTD " maps loaded)\n", map_ids.size());
        ParsMapFiles();
    }

    CascStorage.reset();

    printf("\n");
    if (!success)
    {
        printf("ERROR: Extract %s. Work NOT complete.\n   Precise vector data=%d.\nPress any key.\n", VMAP::VMAP_MAGIC, preciseVectorData);
        getchar();
    }

    printf("Extract %s. Work complete. No errors.\n", VMAP::VMAP_MAGIC);
    return 0;
}
