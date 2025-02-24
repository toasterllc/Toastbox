#pragma once
#include <cstdint>
#include <string>
#include "Endian.h"

namespace Toastbox::USB {

// Universal Serial Bus Specification
// Revision 2.0

// Descriptor Type (bDescriptorType, wValue [high byte])
namespace DescriptorType {
    constexpr uint8_t Device                    = 1;
    constexpr uint8_t Configuration             = 2;
    constexpr uint8_t String                    = 3;
    constexpr uint8_t Interface                 = 4;
    constexpr uint8_t Endpoint                  = 5;
    constexpr uint8_t DeviceQualifier           = 6;
    constexpr uint8_t OtherSpeedConfiguration   = 7;
    constexpr uint8_t InterfacePower            = 8;
};

// Request (bRequest)
namespace Request {
    constexpr uint8_t GetStatus                 = 0;
    constexpr uint8_t ClearFeature              = 1;
    constexpr uint8_t _Reserved0                = 2;
    constexpr uint8_t SetFeature                = 3;
    constexpr uint8_t _Reserved1                = 4;
    constexpr uint8_t SetAddress                = 5;
    constexpr uint8_t GetDescriptor             = 6;
    constexpr uint8_t SetDescriptor             = 7;
    constexpr uint8_t GetConfiguration          = 8;
    constexpr uint8_t SetConfiguration          = 9;
    constexpr uint8_t GetInterface              = 10;
    constexpr uint8_t SetInterface              = 11;
    constexpr uint8_t SynchFrame                = 12;
};

// Request Type (bmRequestType)
namespace RequestType {
    constexpr uint8_t DirectionOut              = 0x00;
    constexpr uint8_t DirectionIn               = 0x80;
    constexpr uint8_t DirectionMask             = 0x80;
    
    constexpr uint8_t TypeStandard              = 0x00;
    constexpr uint8_t TypeClass                 = 0x20;
    constexpr uint8_t TypeVendor                = 0x40;
    constexpr uint8_t TypeReserved              = 0x60;
    constexpr uint8_t TypeMask                  = 0x60;
    
    constexpr uint8_t RecipientDevice           = 0x00;
    constexpr uint8_t RecipientInterface        = 0x01;
    constexpr uint8_t RecipientEndpoint         = 0x02;
    constexpr uint8_t RecipientOther            = 0x03;
    constexpr uint8_t RecipientMask             = 0x1F;
};

namespace Endpoint {
    struct SpeedFull {
        static constexpr size_t MaxPacketSizeCtrl = 64;
        static constexpr size_t MaxPacketSizeBulk = 64;
    };
    
    struct SpeedHigh {
        static constexpr size_t MaxPacketSizeCtrl = 64;
        static constexpr size_t MaxPacketSizeBulk = 512;
    };
    
    constexpr uint8_t MaxCountOut               = 16; // Max number of endpoints, OUT
    constexpr uint8_t MaxCountIn                = 16; // Max number of endpoints, IN
    constexpr uint8_t MaxCount                  = 32; // Max number of endpoints, total
    
    constexpr uint8_t DirectionOut              = 0x00;
    constexpr uint8_t DirectionIn               = 0x80;
    constexpr uint8_t DirectionMask             = 0x80;
    
    constexpr uint8_t IndexMask                 = 0x0F;
    
    constexpr uint8_t Default                   = 0x00; // Default control endpoint
    
    constexpr uint8_t Idx(uint8_t ep)   { return ep&IndexMask;                          }
    constexpr bool Out(uint8_t ep)      { return (ep&DirectionMask) == DirectionOut;    }
    constexpr bool In(uint8_t ep)       { return (ep&DirectionMask) == DirectionIn;     }
    
    template<size_t N>
    constexpr size_t CountOut(const uint8_t (&eps)[N]) {
        size_t count = 0;
        for (uint8_t ep : eps) count += Out(ep);
        return count;
    }
    
    template<size_t N>
    constexpr size_t CountIn(const uint8_t (&eps)[N]) {
        size_t count = 0;
        for (uint8_t ep : eps) count += In(ep);
        return count;
    }
    
    template<typename T_Speed, size_t N>
    constexpr inline size_t MaxPacketSizeOut(const uint8_t (&eps)[N]) {
        // Don't have OUT endpoints: MPS=control transfer MPS (64)
        // Do have OUT endpoints: MPS=bulk transfer MPS (512, the only value that the spec allows for HS bulk endpoints)
        return !CountOut(eps) ? T_Speed::MaxPacketSizeCtrl : T_Speed::MaxPacketSizeBulk;
    }
    
    template<typename T_Speed, size_t N>
    constexpr inline size_t MaxPacketSizeIn(const uint8_t (&eps)[N]) {
        // Don't have IN endpoints: MPS=control transfer MPS (64)
        // Do have IN endpoints: MPS=bulk transfer MPS (512, the only value that the spec allows for HS bulk endpoints)
        return !CountIn(eps) ? T_Speed::MaxPacketSizeCtrl : T_Speed::MaxPacketSizeBulk;
    }
};

// Endpoint Attributes (bmAttributes)
namespace EndpointAttributes {
    constexpr uint8_t TransferControl                   = 0x00;
    constexpr uint8_t TransferIsochronous               = 0x01;
    constexpr uint8_t TransferBulk                      = 0x02;
    constexpr uint8_t TransferInterrupt                 = 0x03;
    
    namespace Isochronous {
        constexpr uint8_t SynchronizationNone           = 0x00;
        constexpr uint8_t SynchronizationAsynchronous   = 0x04;
        constexpr uint8_t SynchronizationAdaptive       = 0x08;
        constexpr uint8_t SynchronizationSynchronous    = 0x0C;
        
        constexpr uint8_t UsageData                     = 0x00;
        constexpr uint8_t UsageFeedback                 = 0x10;
        constexpr uint8_t UsageImplicitFeedbackData     = 0x20;
    };
};

namespace Language {
    constexpr uint16_t English = 0x0409;
};

struct [[gnu::packed]] DeviceDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
};

namespace ConfigurationCharacteristics {
    constexpr uint8_t RemoteWakeup   = 1<<5;
    constexpr uint8_t SelfPowered    = 1<<6;
};

struct [[gnu::packed]] ConfigurationDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
};

struct [[gnu::packed]] InterfaceDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
};

struct [[gnu::packed]] EndpointDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
};

struct [[gnu::packed]] DeviceQualifierDescriptor {
    uint8_t bLength;
    uint8_t bType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint8_t bNumConfigurations;
    uint8_t bReserved;
};

struct [[gnu::packed]] StringDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
};

template<size_t N>
struct [[gnu::packed]] StringDescriptorN : StringDescriptor {
    static_assert(N <= 126, "max character count is 126 (2 string descriptor header bytes + 126 UTF-16 characters == 2 + 2*126 == 254; more than that overflows bLength)");
    uint16_t str[N] = {};
    
    StringDescriptorN() :
    StringDescriptor({.bLength=sizeof(*this), .bDescriptorType=DescriptorType::String}) {}
    
    constexpr StringDescriptorN(const char (&s)[N+1]) :
    StringDescriptor({.bLength=sizeof(*this), .bDescriptorType=DescriptorType::String}) {
        for (size_t i=0; i<N; i++) {
            str[i] = Endian::LFH_U16(s[i]);
        }
    }
    
    std::string asciiString() {
        const size_t len = (bLength-2)/2;
        std::string r;
        r.reserve(len); // std::string adds null terminator implicitly, so don't add space for it ourself
        for (size_t i=0; i<len; i++) {
            r += Endian::HFL(str[i]);
        }
        return r;
    }
    
};

using StringDescriptorMax = StringDescriptorN<126>;

template<size_t N>
constexpr auto StringDescriptorMake(const char (&str)[N]) {
    return StringDescriptorN<N-1>(str);
}

template<size_t N>
struct [[gnu::packed]] SupportedLanguagesDescriptorN : StringDescriptor {
    uint16_t langs[N] = {};
    
    constexpr SupportedLanguagesDescriptorN(const uint16_t (&l)[N]) :
    StringDescriptor({.bLength=(2*N+2), .bDescriptorType=DescriptorType::String}) {
        for (size_t i=0; i<N; i++) {
            langs[i] = Endian::LFH_U16(l[i]);
        }
    }
};

template<size_t N>
constexpr auto SupportedLanguagesDescriptorMake(const uint16_t (&langs)[N]) {
    return SupportedLanguagesDescriptorN<N>(langs);
}

struct [[gnu::packed]] SetupRequest {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

namespace CDC {

// Universal Serial Bus Class Definitions for Communication Devices
// Version 1.1

namespace Request {
    constexpr uint8_t SEND_ENCAPSULATED_COMMAND = 0x00;
    constexpr uint8_t GET_ENCAPSULATED_RESPONSE = 0x01;
    constexpr uint8_t SET_COMM_FEATURE          = 0x02;
    constexpr uint8_t GET_COMM_FEATURE          = 0x03;
    constexpr uint8_t CLEAR_COMM_FEATURE        = 0x04;
    constexpr uint8_t SET_LINE_CODING           = 0x20;
    constexpr uint8_t GET_LINE_CODING           = 0x21;
    constexpr uint8_t SET_CONTROL_LINE_STATE    = 0x22;
    constexpr uint8_t SEND_BREAK                = 0x23;
};

struct [[gnu::packed]] HeaderFunctionalDescriptor {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint16_t bcdCDC;
};

struct [[gnu::packed]] AbstractControlManagementFunctionalDescriptor {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bmCapabilities;
};

struct [[gnu::packed]] UnionFunctionalDescriptor {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bMasterInterface;
    uint8_t bSlaveInterface0;
};

struct [[gnu::packed]] CallManagementFunctionalDescriptor {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bmCapabilities;
    uint8_t bDataInterface;
};

struct [[gnu::packed]] LineCoding {
    uint32_t dwDTERate;
    uint8_t bCharFormat;
    uint8_t bParityType;
    uint8_t bDataBits;
};

} // namespace CDC

} // namespace Toastbox::USB
