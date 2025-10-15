#include "msd_manager.h"
#include "../utilities/logger.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

// Original MSD constants from okami-apclient
constexpr uint16_t EndDialog = 0x8001;
constexpr uint16_t UnsupportedChar = 201;

// Original ASCII to MSD character mapping from okami-apclient/src/library/msd.cpp
static std::unordered_map<char, uint16_t> ASCIIToMSDMap = {
    {' ', 0},   {'.', 1},   {',', 2},   {'?', 3},   {'!', 4},   {'(', 5},   {')', 6},   {'*', 7},
    {'/', 8},   {'\\', 8},  {'"', 9},   {'\'', 10}, {'[', 11},  {']', 12},  {'{', 11},  {'}', 12},
    {'0', 13},  {'1', 14},  {'2', 15},  {'3', 16},  {'4', 17},  {'5', 18},  {'6', 19},  {'7', 20},
    {'8', 21},  {'9', 22},  {'A', 23},  {'B', 24},  {'C', 25},  {'D', 26},  {'E', 27},  {'F', 28},
    {'G', 29},  {'H', 30},  {'I', 31},  {'J', 32},  {'K', 33},  {'L', 34},  {'M', 35},  {'N', 36},
    {'O', 37},  {'P', 38},  {'Q', 39},  {'R', 40},  {'S', 41},  {'T', 42},  {'U', 43},  {'V', 44},
    {'W', 45},  {'X', 46},  {'Y', 47},  {'Z', 48},  {'a', 49},  {'b', 50},  {'c', 51},  {'d', 52},
    {'e', 53},  {'f', 54},  {'g', 55},  {'h', 56},  {'i', 57},  {'j', 58},  {'k', 59},  {'l', 60},
    {'m', 61},  {'n', 62},  {'o', 63},  {'p', 64},  {'q', 65},  {'r', 66},  {'s', 67},  {'t', 68},
    {'u', 69},  {'v', 70},  {'w', 71},  {'x', 72},  {'y', 73},  {'z', 74},  {'^', 82},  {'<', 85},
    {'_', 94},  {'~', 95},  {'>', 98},  {'|', 107}, {'+', 197}, {'-', 198}, {':', 203},
    // Best matches from original
    {';', 102}, {'`', 121}, {'$', 106}, {'%', 98},  {'&', 108}, {'#', 81},  {'=', 209},
};

namespace wolf::runtime
{

// Global MSD manager instance
MSDManager g_MSDManager;

MSDManager::MSDManager() : dirty_(false)
{
}

void MSDManager::makeDirty()
{
    dirty_ = true;
}

std::vector<uint16_t> MSDManager::compileString(const std::string &str)
{
    std::vector<uint16_t> result;
    
    // Original implementation from okami-apclient/src/library/msd.cpp:147-163
    for (char c : str)
    {
        if (auto it = ASCIIToMSDMap.find(c); it != ASCIIToMSDMap.end())
        {
            result.push_back(it->second);
        }
        else
        {
            result.push_back(UnsupportedChar);
        }
    }
    result.push_back(EndDialog);
    return result;
}

void MSDManager::readMSD(const void *pData)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!pData)
    {
        logWarning("[WOLF] MSDManager: Null MSD data provided");
        return;
    }
    
    // Original implementation from okami-apclient/src/library/msd.cpp:125-145
    const MSDHeader *pHead = reinterpret_cast<const MSDHeader *>(pData);
    const uint8_t *pDataPtr = reinterpret_cast<const uint8_t *>(pData);
    strings_.clear();
    strings_.reserve(pHead->numEntries);
    
    for (uint32_t i = 0; i < pHead->numEntries; i++)
    {
        std::vector<uint16_t> str;
        
        const uint16_t *pStrOffset = reinterpret_cast<const uint16_t *>(&pDataPtr[pHead->offsets[i]]);
        const uint16_t *pStr = pStrOffset;
        
        // This is how Okami tests for EndDialog internally
        for (; (*pStr & 0xFF00) != 0x8000; pStr++)
        {
            str.push_back(*pStr);
        }
        str.push_back(*pStr); // include terminator
        strings_.push_back(std::move(str));
    }
    
    makeDirty();
    logDebug("[WOLF] MSDManager: Loaded %u strings from original MSD", pHead->numEntries);
}

uint32_t MSDManager::addString(WolfModId modId, const std::string &str)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint16_t> compiledStr = compileString(str);
    strings_.push_back(std::move(compiledStr));
    
    uint32_t index = static_cast<uint32_t>(strings_.size() - 1);
    makeDirty();
    
    logDebug("[WOLF] MSDManager: Added string '%s' at index %u for mod %u", str.c_str(), index, modId);
    return index;
}

void MSDManager::overrideString(WolfModId modId, uint32_t index, const std::string &str)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index >= strings_.size())
    {
        logWarning("[WOLF] MSDManager: Attempted to override invalid string index %u", index);
        return;
    }
    
    std::vector<uint16_t> compiledStr = compileString(str);
    strings_[index] = std::move(compiledStr);
    
    makeDirty();
    logDebug("[WOLF] MSDManager: Overrode string at index %u with '%s' for mod %u", index, str.c_str(), modId);
}

void MSDManager::rebuild()
{
    if (!dirty_)
        return;
    
    compiledMSD_.clear();
    
    if (strings_.empty())
    {
        dirty_ = false;
        return;
    }
    
    // Calculate required space
    size_t headerSize = sizeof(uint32_t) + sizeof(uint64_t) * strings_.size();
    size_t totalStringSize = 0;
    
    for (const auto &str : strings_)
    {
        totalStringSize += str.size() * sizeof(uint16_t);
    }
    
    // Reserve space
    compiledMSD_.resize(headerSize + totalStringSize);
    
    // Write header
    MSDHeader *header = reinterpret_cast<MSDHeader *>(compiledMSD_.data());
    header->numEntries = static_cast<uint32_t>(strings_.size());
    
    // Write string data and update offsets
    uint8_t *stringDataStart = compiledMSD_.data() + headerSize;
    uint8_t *currentPos = stringDataStart;
    
    for (size_t i = 0; i < strings_.size(); i++)
    {
        header->offsets[i] = static_cast<uint64_t>(currentPos - compiledMSD_.data());
        
        size_t stringBytes = strings_[i].size() * sizeof(uint16_t);
        std::memcpy(currentPos, strings_[i].data(), stringBytes);
        currentPos += stringBytes;
    }
    
    dirty_ = false;
    logDebug("[WOLF] MSDManager: Rebuilt MSD with %zu strings, %zu bytes", strings_.size(), compiledMSD_.size());
}

const uint8_t *MSDManager::getData()
{
    std::lock_guard<std::mutex> lock(mutex_);
    rebuild();
    return compiledMSD_.empty() ? nullptr : compiledMSD_.data();
}

size_t MSDManager::getDataSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Need to rebuild if dirty (but we're const, so cast away constness for rebuild)
    if (dirty_)
    {
        const_cast<MSDManager*>(this)->rebuild();
    }
    return compiledMSD_.size();
}

size_t MSDManager::getStringCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return strings_.size();
}

void MSDManager::removeModStrings(WolfModId modId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Note: For simplicity, this implementation doesn't track which mod added which string
    // A more sophisticated implementation would maintain a modId -> stringIndex mapping
    // and remove only strings added by the specific mod
    
    logDebug("[WOLF] MSDManager: Mod cleanup requested for mod %u (not fully implemented)", modId);
}

} // namespace wolf::runtime

// C API implementation
extern "C"
{
    uint32_t __cdecl wolfRuntimeAddMSDString(WolfModId modId, const char *str)
    {
        if (!str)
            return 0;
        
        return wolf::runtime::g_MSDManager.addString(modId, std::string(str));
    }
    
    void __cdecl wolfRuntimeOverrideMSDString(WolfModId modId, uint32_t index, const char *str)
    {
        if (!str)
            return;
        
        wolf::runtime::g_MSDManager.overrideString(modId, index, std::string(str));
    }
}