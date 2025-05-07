#pragma once

#if __APPLE__
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include "SendRight.h"
#elif __linux__
#include <libusb-1.0/libusb.h>
#endif

#include <vector>
#include <set>
#include <memory>
#include <mutex>
#include <cassert>
#include "USB.h"
#include "RefCounted.h"
#include "Uniqued.h"
#include "RuntimeError.h"
#include "KernError.h"
#include "Defer.h"

namespace Toastbox {

struct USBDevice; using USBDevicePtr = std::unique_ptr<USBDevice>;
struct USBDevice {
    using Milliseconds = std::chrono::milliseconds;
    static constexpr inline Milliseconds Forever = Milliseconds::max();
    
    struct _EndpointInfo {
        bool valid = false;
        uint8_t epAddr = 0;
        uint8_t ifaceIdx = 0;
        uint8_t pipeRef = 0;
        uint16_t maxPacketSize = 0;
    };
    
    // We've observed hangs when trying to transfer more than 16383 packets with a single transfer,
    // which is probably due to a hardware register overflowing in the host's USB controller.
    //
    // When we looked at the bus traffic, 16385 packets causes 1 packet to be transferred,
    // 16386 causes 2 packets, and so on. So it seems that the overflowing  register is 14-bits wide.
    //
    // We think the bug is on the host side, not the device side, because the bus traffic shows
    // that when the issue occurs, the host stops sending IN tokens, implying that the host believes
    // that it received all the data that it expected.
    //
    // Configuration that triggers bug:
    //     - ARM (Apple silicon) Mac
    //     - Device connected via USB hub
    //     - Device enumerates in USB full-speed mode (not high-speed)
    //
    // Other configurations:
    //     - ARM Mac -> device                              : works
    //     - ARM Mac -> Anyoyo hub -> device                : hang
    //     - ARM Mac -> Sabrent hub -> device               : hang
    //     - ARM Mac -> LG monitor -> device                : works
    //     - ARM Mac -> LG monitor -> Anyoyo hub -> device  : works
    //     - Intel Mac -> anything -> device                : works
    
    static constexpr inline size_t _PacketCountMax = 16383;
    
#if __APPLE__
    
    template<typename T>
    static void _Retain(T** x) { (*x)->AddRef(x); }
    
    template<typename T>
    static void _Release(T** x) { (*x)->Release(x); }
    
    using _IOCFPlugInInterface = RefCounted<IOCFPlugInInterface**, _Retain<IOCFPlugInInterface>, _Release<IOCFPlugInInterface>>;
    using _IOUSBDeviceInterface = RefCounted<IOUSBDeviceInterface**, _Retain<IOUSBDeviceInterface>, _Release<IOUSBDeviceInterface>>;
    using _IOUSBInterfaceInterface = RefCounted<IOUSBInterfaceInterface**, _Retain<IOUSBInterfaceInterface>, _Release<IOUSBInterfaceInterface>>;
    
    struct _Interface {
        template<auto T_Fn, typename... T_Args>
        IOReturn iokitExec(T_Args&&... args) const {
            assert(_iokitInterface);
            return ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
//            for (;;) {
//                const IOReturn ior = ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
//                if (ior != kIOReturnAborted) return ior;
//            }
        }
        
        template<auto T_Fn, typename... T_Args>
        IOReturn iokitExec(T_Args&&... args) {
            assert(_iokitInterface);
            return ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
//            for (;;) {
//                const IOReturn ior = ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
//                printf("ior=0x%jx\n", (uintmax_t)ior);
//                if (ior != kIOReturnAborted) return ior;
//            }
        }
        
        _Interface(SendRight&& service) {
            assert(service);
            
            _IOCFPlugInInterface plugin;
            {
                IOCFPlugInInterface** tmp = nullptr;
                SInt32 score = 0;
                kern_return_t kr = IOCreatePlugInInterfaceForService(service, kIOUSBInterfaceUserClientTypeID,
                    kIOCFPlugInInterfaceID, &tmp, &score);
                if (kr != KERN_SUCCESS) throw std::runtime_error("IOCreatePlugInInterfaceForService failed");
                if (!tmp) throw std::runtime_error("IOCreatePlugInInterfaceForService returned NULL plugin");
                plugin = _IOCFPlugInInterface(_IOCFPlugInInterface::NoRetain, tmp);
            }
            
            {
                IOUSBInterfaceInterface** tmp = nullptr;
                HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID), (LPVOID*)&tmp);
                if (hr) throw std::runtime_error("QueryInterface failed");
                _iokitInterface = _IOUSBInterfaceInterface(_IOUSBInterfaceInterface::NoRetain, tmp);
            }
        }
        
        void claim() {
            if (_claimed) return;
            // Open the interface
            IOReturn ior = iokitExec<&IOUSBInterfaceInterface::USBInterfaceOpen>();
            _CheckErr(ior, "USBInterfaceOpen failed");
            _claimed = true;
        }
        
        template<typename T>
        void read(const _EndpointInfo& epInfo, T& t) {
            const size_t len = read(epInfo, (void*)&t, sizeof(t));
            if (len != sizeof(t)) throw RuntimeError("read() didn't read enough data (needed %ju bytes, got %ju bytes)",
                (uintmax_t)sizeof(t), (uintmax_t)len);
        }
        
        size_t read(const _EndpointInfo& epInfo, void* buf, size_t len) {
            // See comment for _PacketCountMax
            const size_t chunkLenMax = epInfo.maxPacketSize * _PacketCountMax;
            uint8_t* buf8 = (uint8_t*)buf;
            size_t off = 0;
            size_t rem = len;
            while (rem) {
                const size_t chunkLen = std::min(chunkLenMax, rem);
                uint32_t lenRead = (uint32_t)chunkLen;
//                printf("  ReadPipe(chunkLen=%ju)\n", (uintmax_t)chunkLen);
                IOReturn ior = iokitExec<&IOUSBInterfaceInterface::ReadPipe>(epInfo.pipeRef, buf8+off, &lenRead);
//                printf("  -> ior=0x%jx lenRead=%ju\n", (intmax_t)ior, (uintmax_t)lenRead);
                _CheckErr(ior, "ReadPipe failed");
                off += lenRead;
                rem -= lenRead;
                if (lenRead < chunkLen) break;
            }
            return off;
        }
        
        template<typename T>
        void write(const _EndpointInfo& epInfo, T& x) {
            write(epInfo, (void*)&x, sizeof(x));
        }
        
        void write(const _EndpointInfo& epInfo, const void* buf, size_t len) {
            // See comment for _PacketCountMax
            const size_t chunkLenMax = epInfo.maxPacketSize * _PacketCountMax;
            uint8_t* buf8 = (uint8_t*)buf;
            size_t off = 0;
            size_t rem = len;
            // do-while loop because we want a zero-length packet to be sent in the case that `len` == 0
            do {
                const size_t chunkLen = std::min(chunkLenMax, rem);
//                printf("  WritePipe(chunkLen=%ju)\n", (uintmax_t)chunkLen);
                IOReturn ior = iokitExec<&IOUSBInterfaceInterface::WritePipe>(epInfo.pipeRef, buf8+off, (uint32_t)chunkLen);
//                printf("  -> ior=0x%jx\n", (intmax_t)ior);
                _CheckErr(ior, "WritePipe failed");
                off += chunkLen;
                rem -= chunkLen;
            } while (rem);
        }
        
        void reset(const _EndpointInfo& epInfo) {
            IOReturn ior = iokitExec<&IOUSBInterfaceInterface::ResetPipe>(epInfo.pipeRef);
            _CheckErr(ior, "ResetPipe failed");
        }
        
        _IOUSBInterfaceInterface _iokitInterface;
        bool _claimed = false;
    };
    
#elif __linux__
    
    struct _Interface {
        uint8_t bInterfaceNumber = 0;
        bool claimed = false;
    };
    
#endif
    
    // Copy: illegal
    USBDevice(const USBDevice& x) = delete;
    USBDevice& operator=(const USBDevice& x) = delete;
    // Move: OK
    USBDevice(USBDevice&& x) = default;
    USBDevice& operator=(USBDevice&& x) = default;
    
#if __APPLE__
    static std::vector<USBDevicePtr> DevicesGet() {
        std::vector<USBDevicePtr> devices;
        io_iterator_t ioServicesIter = MACH_PORT_NULL;
        kern_return_t kr = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(kIOUSBDeviceClassName), &ioServicesIter);
        if (kr != KERN_SUCCESS) throw KernError(kr, "IOServiceGetMatchingServices failed");
        
        SendRight servicesIter(SendRight::NoRetain, ioServicesIter);
        while (servicesIter) {
            SendRight service(SendRight::NoRetain, IOIteratorNext(servicesIter));
            if (!service.valid()) break;
            // Ignore devices that we fail to create a USBDevice for
            try { devices.emplace_back(std::make_unique<USBDevice>(service)); }
            catch (...) {}
        }
        return devices;
    }
    
    template<auto T_Fn, typename... T_Args>
    IOReturn iokitExec(T_Args&&... args) const {
        assert(_iokitInterface);
        return ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
//        for (;;) {
//            const IOReturn ior = ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
//            if (ior != kIOReturnAborted) return ior;
//        }
    }
    
    template<auto T_Fn, typename... T_Args>
    IOReturn iokitExec(T_Args&&... args) {
        assert(_iokitInterface);
        return ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
//        for (;;) {
//            const IOReturn ior = ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
//            if (ior != kIOReturnAborted) return ior;
//        }
    }
    
    USBDevice(const SendRight& service) : _service(service) {
        assert(service);
        
//        CFMutableDictionaryRef props;
//        kern_return_t kr = IORegistryEntryCreateCFProperties(_service, &props, nullptr, 0);
//        assert(kr == kIOReturnSuccess);
//        CFShow(props);
        
        _IOCFPlugInInterface plugin;
        {
            IOCFPlugInInterface** tmp = nullptr;
            SInt32 score = 0;
            IOReturn kr = IOCreatePlugInInterfaceForService(_service, kIOUSBDeviceUserClientTypeID,
                kIOCFPlugInInterfaceID, &tmp, &score);
            if (kr != KERN_SUCCESS) throw std::runtime_error("IOCreatePlugInInterfaceForService failed");
            if (!tmp) throw std::runtime_error("IOCreatePlugInInterfaceForService returned NULL plugin");
            plugin = _IOCFPlugInInterface(_IOCFPlugInInterface::NoRetain, tmp);
        }
        
        {
            IOUSBDeviceInterface** tmp = nullptr;
            HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID*)&tmp);
            if (hr) throw std::runtime_error("QueryInterface failed");
            _iokitInterface = _IOUSBDeviceInterface(_IOUSBDeviceInterface::NoRetain, tmp);
        }
        
        // Populate _interfaces / _epInfos
        {
            io_iterator_t ioServicesIter = MACH_PORT_NULL;
            IOUSBFindInterfaceRequest req = {
                .bInterfaceClass = kIOUSBFindInterfaceDontCare,
                .bInterfaceSubClass = kIOUSBFindInterfaceDontCare,
                .bInterfaceProtocol = kIOUSBFindInterfaceDontCare,
                .bAlternateSetting = kIOUSBFindInterfaceDontCare,
            };
            
            IOReturn ior = iokitExec<&IOUSBDeviceInterface::CreateInterfaceIterator>(&req, &ioServicesIter);
            _CheckErr(ior, "CreateInterfaceIterator failed");
            
            SendRight servicesIter(SendRight::NoRetain, ioServicesIter);
            while (servicesIter) {
                SendRight service(SendRight::NoRetain, IOIteratorNext(servicesIter));
                if (!service.valid()) break;
                _interfaces.emplace_back(std::move(service));
                
                _Interface& iface = _interfaces.back();
                uint8_t epCount = 0;
                IOReturn ior = iface.iokitExec<&IOUSBInterfaceInterface::GetNumEndpoints>(&epCount);
                _CheckErr(ior, "GetNumEndpoints failed");
                
                for (uint8_t pipeRef=1; pipeRef<=epCount; pipeRef++) {
                    IOUSBEndpointProperties props = { .bVersion = kUSBEndpointPropertiesVersion3 };
                    ior = iface.iokitExec<&IOUSBInterfaceInterface::GetPipePropertiesV3>(pipeRef, &props);
                    _CheckErr(ior, "GetPipePropertiesV3 failed");
                    
                    const bool dirOut = props.bDirection==kUSBOut || props.bDirection==kUSBAnyDirn;
                    const uint8_t epAddr = (dirOut ? USB::Endpoint::DirectionOut : USB::Endpoint::DirectionIn)|props.bEndpointNumber;
                    auto& epInfo = _epInfos[_IdxForEndpointAddr(epAddr)];
                    // Continue if _epInfos already has an entry for this epAddr
                    if (epInfo.valid) continue;
                    epInfo = {
                        .valid          = true,
                        .epAddr         = epAddr,
                        .ifaceIdx       = (uint8_t)(_interfaces.size()-1),
                        .pipeRef        = pipeRef,
                        .maxPacketSize  = props.wMaxPacketSize,
                    };
                }
            }
        }
    }
    
    bool operator==(const USBDevice& x) const {
        return _service == x._service;
    }
    
    USB::DeviceDescriptor deviceDescriptor() const {
        // Apple's APIs don't provide a way to get the device descriptor, only
        // the configuration descriptor (GetConfigurationDescriptorPtr).
        // However in our testing, no IO actually occurs with the device when
        // requesting its device descriptor, so the kernel must intercept the
        // request and return a cached copy. So in short, requesting the
        // device descriptor shouldn't be too expensive.
        using namespace Endian;
        USB::DeviceDescriptor desc;
        IOUSBDevRequest req = {
            .bmRequestType  = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice),
            .bRequest       = kUSBRqGetDescriptor,
            .wValue         = kUSBDeviceDesc<<8,
            .wIndex         = 0,
            .wLength        = sizeof(desc),
            .pData          = &desc,
        };
        
        IOReturn ior = iokitExec<&IOUSBDeviceInterface::DeviceRequest>(&req);
        _CheckErr(ior, "DeviceRequest failed");
        
        desc.bLength                = HFL(desc.bLength);
        desc.bDescriptorType        = HFL(desc.bDescriptorType);
        desc.bcdUSB                 = HFL(desc.bcdUSB);
        desc.bDeviceClass           = HFL(desc.bDeviceClass);
        desc.bDeviceSubClass        = HFL(desc.bDeviceSubClass);
        desc.bDeviceProtocol        = HFL(desc.bDeviceProtocol);
        desc.bMaxPacketSize0        = HFL(desc.bMaxPacketSize0);
        desc.idVendor               = HFL(desc.idVendor);
        desc.idProduct              = HFL(desc.idProduct);
        desc.bcdDevice              = HFL(desc.bcdDevice);
        desc.iManufacturer          = HFL(desc.iManufacturer);
        desc.iProduct               = HFL(desc.iProduct);
        desc.iSerialNumber          = HFL(desc.iSerialNumber);
        desc.bNumConfigurations     = HFL(desc.bNumConfigurations);
        return desc;
    }
    
    USB::ConfigurationDescriptor configurationDescriptor(uint8_t idx) const {
        using namespace Endian;
        IOUSBConfigurationDescriptorPtr descPtr = nullptr;
        IOReturn ior = iokitExec<&IOUSBDeviceInterface::GetConfigurationDescriptorPtr>(idx, &descPtr);
        _CheckErr(ior, "GetConfigurationDescriptorPtr failed");
        if (!descPtr) throw RuntimeError("GetConfigurationDescriptorPtr returned null");
        return USB::ConfigurationDescriptor{
            .bLength                 = HFL(descPtr->bLength),
            .bDescriptorType         = HFL(descPtr->bDescriptorType),
            .wTotalLength            = HFL(descPtr->wTotalLength),
            .bNumInterfaces          = HFL(descPtr->bNumInterfaces),
            .bConfigurationValue     = HFL(descPtr->bConfigurationValue),
            .iConfiguration          = HFL(descPtr->iConfiguration),
            .bmAttributes            = HFL(descPtr->bmAttributes),
            .bMaxPower               = HFL(descPtr->MaxPower), // `MaxPower`: typo or intentional indication of unit change?
        };
    }
    
    USB::StringDescriptorMax stringDescriptor(uint8_t idx, uint16_t lang=USB::Language::English) {
        using namespace Endian;
        
        USB::StringDescriptorMax desc;
        IOUSBDevRequest req = {
            .bmRequestType  = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice),
            .bRequest       = kUSBRqGetDescriptor,
            .wValue         = (uint16_t)((kUSBStringDesc<<8)|idx),
            .wIndex         = lang,
            .wLength        = sizeof(desc),
            .pData          = &desc,
        };
        
        IOReturn ior = iokitExec<&IOUSBDeviceInterface::DeviceRequest>(&req);
        _CheckErr(ior, "DeviceRequest failed");
        return desc;
    }
    
    void claim(uint32_t attempts=1) {
        assert(attempts);
        if (_claimed) return;
        
        constexpr auto AttemptDelay = std::chrono::milliseconds(500);
        while (attempts--) {
            try {
                // Open the device
                IOReturn ior = iokitExec<&IOUSBDeviceInterface::USBDeviceOpen>();
                _CheckErr(ior, "USBDeviceOpen failed");
            
            } catch (const Toastbox::KernError& e) {
                if (e.kr != kIOReturnExclusiveAccess) throw;
                if (!attempts) throw;
                printf("[USBDevice : claim] Failed to claim device; trying again (%s)\n", e.what());
                std::this_thread::sleep_for(AttemptDelay);
            }
        }
        
        // Claim each interface
        for (_Interface& iface : _interfaces) {
            iface.claim();
        }
        
        _claimed = true;
    }
    
    template<typename... T_Args>
    auto read(uint8_t epAddr, T_Args&&... args) {
        const _EndpointInfo& epInfo = _epInfo(epAddr);
        _Interface& iface = _interfaces.at(epInfo.ifaceIdx);
        return iface.read(epInfo, std::forward<T_Args>(args)...);
    }
    
    template<typename... T_Args>
    void write(uint8_t epAddr, T_Args&&... args) {
        const _EndpointInfo& epInfo = _epInfo(epAddr);
        _Interface& iface = _interfaces.at(epInfo.ifaceIdx);
        iface.write(epInfo, std::forward<T_Args>(args)...);
    }
    
    template<typename... T_Args>
    void reset(uint8_t epAddr, T_Args&&... args) {
        const _EndpointInfo& epInfo = _epInfo(epAddr);
        _Interface& iface = _interfaces.at(epInfo.ifaceIdx);
        iface.reset(epInfo, std::forward<T_Args>(args)...);
    }
    
    template<typename T>
    void vendorRequestOut(uint8_t req, const T& x) {
        vendorRequestOut(req, (const void*)&x, sizeof(x));
    }
    
    void vendorRequestOut(uint8_t req, const void* data, size_t len) {
        IOUSBDevRequest usbReq = {
            .bmRequestType      = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice),
            .bRequest           = req,
            .pData              = (void*)data,
            .wLength            = (uint16_t)len,
        };
        
        IOReturn ior = iokitExec<&IOUSBDeviceInterface::DeviceRequest>(&usbReq);
        _CheckErr(ior, "DeviceRequest failed");
    }
    
    const SendRight& service() const { return _service; }
    
    void debugGetStatus() {
        uint8_t status[2];
        
        IOUSBDevRequest req = {
            .bmRequestType  = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice),
            .bRequest       = kUSBRqGetStatus,
            .wValue         = 0,
            .wIndex         = 0,
            .wLength        = 2,
            .pData          = status,
        };
        
        IOReturn ior = iokitExec<&IOUSBDeviceInterface::DeviceRequest>(&req);
        _CheckErr(ior, "DeviceRequest failed");
    }
    
    static uint8_t _IdxForEndpointAddr(uint8_t epAddr) {
        return ((epAddr&USB::Endpoint::DirectionMask)>>3) | (epAddr&USB::Endpoint::IndexMask);
    }
    
    static void _CheckErr(IOReturn ior, const char* errMsg) {
        if (ior != kIOReturnSuccess) throw KernError(ior, "%s", errMsg);
    }
    
    const _EndpointInfo& _epInfo(uint8_t epAddr) const {
        const _EndpointInfo& epInfo = _epInfos[_IdxForEndpointAddr(epAddr)];
        if (!epInfo.valid) throw RuntimeError("invalid endpoint address: 0x%02x", epAddr);
        return epInfo;
    }
    
    SendRight _service;
    _IOUSBDeviceInterface _iokitInterface;
    std::vector<_Interface> _interfaces;
    _EndpointInfo _epInfos[USB::Endpoint::MaxCount];
    bool _claimed = false;
    
#elif __linux__
    
    static std::vector<USBDevice> DevicesGet() {
        libusb_device** devs = nullptr;
        ssize_t devsCount = libusb_get_device_list(_USBCtx(), &devs);
        _CheckErr((int)devsCount, "libusb_get_device_list failed");
        Defer( if (devs) libusb_free_device_list(devs, true); );
        
        std::vector<USBDevice> r;
        for (size_t i=0; i<(size_t)devsCount; i++) {
            r.push_back(devs[i]);
        }
        return r;
    }
    
    USBDevice(libusb_device* dev) : _dev(_LibusbDev::Retain, std::move(dev)) {
        assert(dev);
        
        // Populate _interfaces and _epInfos
        {
            struct libusb_config_descriptor* configDesc = nullptr;
            int ir = libusb_get_config_descriptor(_dev, 0, &configDesc);
            _CheckErr(ir, "libusb_get_config_descriptor failed");
            
            for (uint8_t ifaceIdx=0; ifaceIdx<configDesc->bNumInterfaces; ifaceIdx++) {
                const struct libusb_interface& iface = configDesc->interface[ifaceIdx];
                
                // For now we're only looking at altsetting 0
                if (iface.num_altsetting < 1) throw RuntimeError("interface has no altsettings");
                _interfaces.push_back({
                    .bInterfaceNumber = iface.altsetting[0].bInterfaceNumber,
                });
                
                const struct libusb_interface_descriptor& ifaceDesc = iface.altsetting[0];
                for (uint8_t epIdx=0; epIdx<ifaceDesc.bNumEndpoints; epIdx++) {
                    const struct libusb_endpoint_descriptor& endpointDesc = ifaceDesc.endpoint[epIdx];
                    const uint8_t epAddr = endpointDesc.bEndpointAddress;
                    _epInfos[_IdxForEndpointAddr(epAddr)] = _EndpointInfo{
                        .valid          = true,
                        .epAddr         = epAddr,
                        .ifaceIdx       = ifaceIdx,
                        .maxPacketSize  = endpointDesc.wMaxPacketSize,
                    };
                }
            }
        }
    }
    
    bool operator==(const USBDevice& x) const {
        return _dev == x._dev;
    }
    
    USB::DeviceDescriptor deviceDescriptor() const {
        struct libusb_device_descriptor desc;
        int ir = libusb_get_device_descriptor(_dev, &desc);
        _CheckErr(ir, "libusb_get_device_descriptor failed");
        
        return USB::DeviceDescriptor{
            .bLength                = desc.bLength,
            .bDescriptorType        = desc.bDescriptorType,
            .bcdUSB                 = desc.bcdUSB,
            .bDeviceClass           = desc.bDeviceClass,
            .bDeviceSubClass        = desc.bDeviceSubClass,
            .bDeviceProtocol        = desc.bDeviceProtocol,
            .bMaxPacketSize0        = desc.bMaxPacketSize0,
            .idVendor               = desc.idVendor,
            .idProduct              = desc.idProduct,
            .bcdDevice              = desc.bcdDevice,
            .iManufacturer          = desc.iManufacturer,
            .iProduct               = desc.iProduct,
            .iSerialNumber          = desc.iSerialNumber,
            .bNumConfigurations     = desc.bNumConfigurations,
        };
    }
    
    USB::ConfigurationDescriptor configurationDescriptor(uint8_t idx) const {
        struct libusb_config_descriptor* desc;
        int ir = libusb_get_config_descriptor(_dev, idx, &desc);
        _CheckErr(ir, "libusb_get_config_descriptor failed");
        return USB::ConfigurationDescriptor{
            .bLength                 = desc->bLength,
            .bDescriptorType         = desc->bDescriptorType,
            .wTotalLength            = desc->wTotalLength,
            .bNumInterfaces          = desc->bNumInterfaces,
            .bConfigurationValue     = desc->bConfigurationValue,
            .iConfiguration          = desc->iConfiguration,
            .bmAttributes            = desc->bmAttributes,
            .bMaxPower               = desc->MaxPower, // `MaxPower`: typo or intentional indication of unit change?
        };
    }
    
    USB::StringDescriptorMax stringDescriptor(uint8_t idx, uint16_t lang=USB::Language::English) {
        _openIfNeeded();
        
        USB::StringDescriptorMax desc;
        int ir = libusb_get_descriptor(_handle, USB::DescriptorType::String, idx, (uint8_t*)&desc, sizeof(desc));
        _CheckErr(ir, "libusb_get_string_descriptor failed");
        desc.bLength = ir;
        return desc;
    }
    
    template<typename T_Dst>
    void read(uint8_t epAddr, T_Dst& dst) {
        const size_t len = read(epAddr, (void*)&dst, sizeof(dst));
        if (len != sizeof(dst)) throw RuntimeError("read() didn't read enough data (needed %ju bytes, got %ju bytes)",
            (uintmax_t)sizeof(dst), (uintmax_t)len);
    }
    
    size_t read(uint8_t epAddr, void* buf, size_t len) {
        _claimInterfaceForEndpointAddr(epAddr);
        int xferLen = 0;
        int ir = libusb_bulk_transfer(_handle, epAddr, (uint8_t*)buf, (int)len, &xferLen, 0);
        _CheckErr(ir, "libusb_bulk_transfer failed");
        return xferLen;
    }
    
    template<typename T_Src>
    void write(uint8_t epAddr, T_Src& src) {
        write(epAddr, (void*)&src, sizeof(src));
    }
    
    void write(uint8_t epAddr, const void* buf, size_t len) {
        _claimInterfaceForEndpointAddr(epAddr);
        
        int xferLen = 0;
        int ir = libusb_bulk_transfer(_handle, epAddr, (uint8_t*)buf, (int)len, &xferLen, 0);
        _CheckErr(ir, "libusb_bulk_transfer failed");
        if ((size_t)xferLen != len)
            throw RuntimeError("libusb_bulk_transfer short write (tried: %zu, got: %zu)", len, (size_t)xferLen);
    }
    
    void reset(uint8_t epAddr) {
        _claimInterfaceForEndpointAddr(epAddr);
        int ir = libusb_clear_halt(_handle, epAddr);
        _CheckErr(ir, "libusb_clear_halt failed");
    }
    
    template<typename T>
    void vendorRequestOut(uint8_t req, const T& x) {
        vendorRequestOut(req, (void*)&x, sizeof(x));
    }
    
    void vendorRequestOut(uint8_t req, const void* data, size_t len) {
        _openIfNeeded();
        
        const uint8_t bmRequestType =
            USB::RequestType::DirectionOut      |
            USB::RequestType::TypeVendor        |
            USB::RequestType::RecipientDevice   ;
        const uint8_t bRequest = req;
        const uint8_t wValue = 0;
        const uint8_t wIndex = 0;
        int ir = libusb_control_transfer(_handle, bmRequestType, bRequest, wValue, wIndex,
            (uint8_t*)data, len, 0);
        _CheckErr(ir, "libusb_control_transfer failed");
    }
    
    operator libusb_device*() const { return _dev; }
    
    struct _EndpointInfo {
        bool valid = false;
        uint8_t epAddr = 0;
        uint8_t ifaceIdx = 0;
        uint16_t maxPacketSize = 0;
    };
    
    static libusb_context* _USBCtx() {
        static std::once_flag Once;
        static libusb_context* Ctx = nullptr;
        std::call_once(Once, [](){
            int ir = libusb_init(&Ctx);
            _CheckErr(ir, "libusb_init failed");
        });
        return Ctx;
    }
    
    static uint8_t _IdxForEndpointAddr(uint8_t epAddr) {
        return ((epAddr&USB::Endpoint::DirectionMask)>>3) | (epAddr&USB::Endpoint::IndexMask);
    }
    
    static void _CheckErr(int ir, const char* errMsg) {
        if (ir < 0) throw RuntimeError("%s: %s", errMsg, libusb_error_name(ir));
    }
    
    void _openIfNeeded() {
        if (_handle.hasValue()) return;
        libusb_device_handle* handle = nullptr;
        int ir = libusb_open(_dev, &handle);
        _CheckErr(ir, "libusb_open failed");
        _handle = handle;
    }
    
    void _claimInterfaceForEndpointAddr(uint8_t epAddr) {
        _openIfNeeded();
        const _EndpointInfo& epInfo = _epInfo(epAddr);
        _Interface& iface = _interfaces.at(epInfo.ifaceIdx);
        if (!iface.claimed) {
            int ir = libusb_claim_interface(_handle, iface.bInterfaceNumber);
            _CheckErr(ir, "libusb_claim_interface failed");
            iface.claimed = true;
        }
    }
    
    const _EndpointInfo& _epInfo(uint8_t epAddr) const {
        const _EndpointInfo& epInfo = _epInfos[_IdxForEndpointAddr(epAddr)];
        if (!epInfo.valid) throw RuntimeError("invalid endpoint address: 0x%02x", epAddr);
        return epInfo;
    }
    
    static void _Retain(libusb_device* x) { libusb_ref_device(x); }
    static void _Release(libusb_device* x) { libusb_unref_device(x); }
    using _LibusbDev = RefCounted<libusb_device*, _Retain, _Release>;
    
    static void _Close(libusb_device_handle* x) { libusb_close(x); }
    using _LibusbHandle = Uniqued<libusb_device_handle*, _Close>;
    
    _LibusbDev _dev = {};
    _LibusbHandle _handle = {};
    std::vector<_Interface> _interfaces = {};
    _EndpointInfo _epInfos[USB::Endpoint::MaxCount] = {};
    
#endif
    
    uint16_t maxPacketSize(uint8_t epAddr) const {
        const _EndpointInfo& epInfo = _epInfo(epAddr);
        return epInfo.maxPacketSize;
    }
    
    std::string manufacturer() {
        return stringDescriptor(deviceDescriptor().iManufacturer).asciiString();
    }
    
    std::string product() {
        return stringDescriptor(deviceDescriptor().iProduct).asciiString();
    }
    
    std::string serialNumber() {
        return stringDescriptor(deviceDescriptor().iSerialNumber).asciiString();
    }
    
    std::vector<uint8_t> endpoints() const {
        std::vector<uint8_t> eps;
        for (const _EndpointInfo& epInfo : _epInfos) {
            if (epInfo.valid) {
                eps.push_back(epInfo.epAddr);
            }
        }
        return eps;
    }
};

} // namespace Toastbox
