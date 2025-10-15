#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "wolf_types.h"

namespace wolf::runtime
{

#pragma pack(push, 1)
struct MSDHeader
{
    uint32_t numEntries;
    uint64_t offsets[1]; // Variable length array
};
#pragma pack(pop)

/**
 * @brief Manages MSD files for custom text strings
 *
 * This class allows mods to add or override text strings in the game's MSD system.
 * Thread-safe for use by multiple mods.
 */
class MSDManager
{
  private:
    mutable std::mutex mutex_;
    std::vector<std::vector<uint16_t>> strings_;
    std::unordered_map<uint32_t, uint32_t> indexMapping_; // original index -> new index

    bool dirty_;
    std::vector<uint8_t> compiledMSD_;

    void makeDirty();
    void rebuild();

  public:
    MSDManager();

    /**
     * @brief Reads original MSD content and prepares it for modification
     * @param pData Pointer to original MSD data
     */
    void readMSD(const void *pData);

    /**
     * @brief Adds a new string to the MSD file
     * @param modId ID of the mod adding the string
     * @param str ASCII string to add
     * @return uint32_t String index for use in game
     */
    uint32_t addString(WolfModId modId, const std::string &str);

    /**
     * @brief Overrides an existing string at the given index
     * @param modId ID of the mod overriding the string
     * @param index Original MSD index to override
     * @param str New string content
     */
    void overrideString(WolfModId modId, uint32_t index, const std::string &str);

    /**
     * @brief Gets the compiled MSD data for use by the game
     * @return Pointer to MSD data (valid until next modification)
     */
    const uint8_t *getData();

    /**
     * @brief Gets the size of the compiled MSD data
     * @return Size in bytes
     */
    size_t getDataSize() const;

    /**
     * @brief Gets the number of strings in the MSD
     * @return Number of strings
     */
    size_t getStringCount() const;

    /**
     * @brief Removes all strings added by a specific mod
     * @param modId ID of the mod to clean up
     */
    void removeModStrings(WolfModId modId);

    /**
     * @brief Compiles ASCII string to MSD format (exposed for testing)
     * @param str ASCII string to compile
     * @return Vector of uint16_t MSD characters with EndDialog terminator
     */
    static std::vector<uint16_t> compileString(const std::string &str);
};

// Global MSD manager instance
extern MSDManager g_MSDManager;

} // namespace wolf::runtime

// C API for mod access
extern "C"
{
    /**
     * @brief Adds a string to the MSD system
     * @param modId ID of the calling mod
     * @param str String to add (null-terminated)
     * @return String index for use in game
     */
    uint32_t __cdecl wolfRuntimeAddMSDString(WolfModId modId, const char *str);

    /**
     * @brief Overrides an existing string in the MSD system
     * @param modId ID of the calling mod
     * @param index Original string index to override
     * @param str New string content (null-terminated)
     */
    void __cdecl wolfRuntimeOverrideMSDString(WolfModId modId, uint32_t index, const char *str);
}
