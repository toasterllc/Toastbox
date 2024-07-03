#include <cstdint>

namespace Toastbox {
namespace FAT12 {

template<size_t T_SectorSize>
struct [[gnu::packed]] BootRecord {
    std::uint8_t     jump[3];
    std::uint8_t     oem[8];
    
    std::uint16_t    sectorSize;            // Size of a sector, in bytes
    std::uint8_t     clusterSize;           // Size of a cluster, in sectors
    std::uint16_t    reservedSize;
    std::uint8_t     fatCount;              // The number of FATs (ie FATTables)
    std::uint16_t    rootEntryCount;        // Number of root directory entries
    std::uint16_t    totalSize;             // Total filesystem size, in sectors
                                            // (BootRecord+FATTable+DirTable+Data)
    
    std::uint8_t     mediaDescriptor;
    std::uint16_t    fatSize;               // Size of FAT (ie FATTable), in sectors
    std::uint16_t    trackSize;             // Size of track, in sectors
    std::uint16_t    headCount;
    std::uint32_t    hiddenSectorCount;
    std::uint32_t    largeSectorCount;
    
    std::uint8_t     driveNumber;
    std::uint8_t     _;
    std::uint8_t     extendedBootSignature;
    std::uint32_t    serialNumber;
    std::uint8_t     volumeLabel[11];
    std::uint8_t     filesystemType[8];
    std::uint8_t     bootcode[448];
    std::uint16_t    signature;
    
    std::uint8_t     __[T_SectorSize-512];
};
static_assert(sizeof(BootRecord<8192>) == 8192);

union [[gnu::packed]] FATEntry {
    struct [[gnu::packed]] {
        std::uint32_t a : 12;
        std::uint32_t b : 12;
    };
    std::uint8_t _[3];
};
static_assert(sizeof(FATEntry) == 3);


//union [[gnu::packed]] FATEntry {
//    std::uint32_t val : 24;
//    std::uint8_t _[3];
//};
//static_assert(sizeof(FATEntry) == 3);

template<size_t T_SectorSize>
struct [[gnu::packed]] FATTable {
    FATEntry entries[T_SectorSize / sizeof(FATEntry)];
    std::uint8_t _[T_SectorSize % sizeof(FATEntry)];
};
static_assert(sizeof(FATTable<8192>) == 8192);

struct [[gnu::packed]] DirEntry {
    std::uint8_t name[8];
    std::uint8_t ext[3];
    std::uint8_t attr;
    std::uint16_t _;
    std::uint16_t creationTime;
    std::uint16_t creationDate;
    std::uint16_t lastAccessDate;
    std::uint16_t __;
    std::uint16_t modifiedTime;
    std::uint16_t modifiedDate;
    std::uint16_t fatIndex;
    std::uint32_t fileSize;
};
static_assert(sizeof(DirEntry) == 32);

template<size_t T_SectorSize>
struct [[gnu::packed]] DirTable {
    static constexpr size_t EntryCount = T_SectorSize / sizeof(DirEntry);
    DirEntry entries[EntryCount];
};
static_assert(sizeof(DirTable<8192>) == 8192);

} // namespace FAT12

} // namespace Toastbox
