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
#include "Defer.h"

namespace Toastbox {

struct USBDevice; using USBDevicePtr = std::unique_ptr<USBDevice>;
class USBDevice {
public:
    using Milliseconds = std::chrono::milliseconds;
    static constexpr inline Milliseconds Forever = Milliseconds::max();
    
#if __APPLE__
    
private:
    template<typename T>
    static void _Retain(T** x) { (*x)->AddRef(x); }
    
    template<typename T>
    static void _Release(T** x) { (*x)->Release(x); }
    
    using _IOCFPlugInInterface = RefCounted<IOCFPlugInInterface**, _Retain<IOCFPlugInInterface>, _Release<IOCFPlugInInterface>>;
    using _IOUSBDeviceInterface = RefCounted<IOUSBDeviceInterface**, _Retain<IOUSBDeviceInterface>, _Release<IOUSBDeviceInterface>>;
    using _IOUSBInterfaceInterface = RefCounted<IOUSBInterfaceInterface**, _Retain<IOUSBInterfaceInterface>, _Release<IOUSBInterfaceInterface>>;
    
    class _Interface {
    public:
        template<auto T_Fn, typename... T_Args>
        IOReturn iokitExec(T_Args&&... args) const {
            assert(_iokitInterface);
            for (;;) {
                const IOReturn ior = ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
                if (ior != kIOReturnAborted) return ior;
            }
        }
        
        template<auto T_Fn, typename... T_Args>
        IOReturn iokitExec(T_Args&&... args) {
            assert(_iokitInterface);
            for (;;) {
                const IOReturn ior = ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
                if (ior != kIOReturnAborted) return ior;
            }
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
        
        template<typename T>
        void read(uint8_t pipeRef, T& t, Milliseconds timeout=Forever) {
            const size_t len = read(pipeRef, (void*)&t, sizeof(t), timeout);
            if (len != sizeof(t)) throw RuntimeError("read() didn't read enough data (needed %ju bytes, got %ju bytes)",
                (uintmax_t)sizeof(t), (uintmax_t)len);
        }
        
        size_t read(uint8_t pipeRef, void* buf, size_t len, Milliseconds timeout=Forever) {
            _openIfNeeded();
            uint32_t len32 = (uint32_t)len;
            if (timeout == Forever) {
                IOReturn ior = iokitExec<&IOUSBInterfaceInterface::ReadPipe>(pipeRef, buf, &len32);
                _CheckErr(ior, "ReadPipe failed");
            } else {
                IOReturn ior = iokitExec<&IOUSBInterfaceInterface::ReadPipeTO>(pipeRef, buf, &len32, 0, (uint32_t)timeout.count());
                _CheckErr(ior, "ReadPipeTO failed");
            }
            return len32;
        }
        
        template<typename T>
        void write(uint8_t pipeRef, T& x, Milliseconds timeout=Forever) {
            write(pipeRef, (void*)&x, sizeof(x), timeout);
        }
        
        void write(uint8_t pipeRef, const void* buf, size_t len, Milliseconds timeout=Forever) {
            _openIfNeeded();
            if (timeout == Forever) {
                IOReturn ior = iokitExec<&IOUSBInterfaceInterface::WritePipe>(pipeRef, (void*)buf, (uint32_t)len);
                _CheckErr(ior, "WritePipe failed");
            } else {
                IOReturn ior = iokitExec<&IOUSBInterfaceInterface::WritePipeTO>(pipeRef, (void*)buf, (uint32_t)len, 0, (uint32_t)timeout.count());
                _CheckErr(ior, "WritePipeTO failed");
            }
        }
        
        void reset(uint8_t pipeRef) {
            _openIfNeeded();
            IOReturn ior = iokitExec<&IOUSBInterfaceInterface::ResetPipe>(pipeRef);
            _CheckErr(ior, "ResetPipe failed");
        }
        
    private:
        void _openIfNeeded() {
            if (_open) return;
            // Open the interface
            IOReturn ior = iokitExec<&IOUSBInterfaceInterface::USBInterfaceOpen>();
            _CheckErr(ior, "USBInterfaceOpen failed");
            _open = true;
        }
        
        _IOUSBInterfaceInterface _iokitInterface;
        bool _open = false;
    };
    
#elif __linux__
    
    struct _Interface {
        uint8_t bInterfaceNumber = 0;
        bool claimed = false;
    };
    
#endif
    
public:
    
    // Copy: illegal
    USBDevice(const USBDevice& x) = delete;
    USBDevice& operator=(const USBDevice& x) = delete;
    // Move: OK
    USBDevice(USBDevice&& x) = default;
    USBDevice& operator=(USBDevice&& x) = default;
    
#if __APPLE__
    static std::vector<USBDevicePtr> GetDevices() {
        std::vector<USBDevicePtr> devices;
        io_iterator_t ioServicesIter = MACH_PORT_NULL;
        kern_return_t kr = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(kIOUSBDeviceClassName), &ioServicesIter);
        if (kr != KERN_SUCCESS) throw RuntimeError("IOServiceGetMatchingServices failed: 0x%x", kr);
        
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
        for (;;) {
            const IOReturn ior = ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
            if (ior != kIOReturnAborted) return ior;
        }
    }
    
    template<auto T_Fn, typename... T_Args>
    IOReturn iokitExec(T_Args&&... args) {
        assert(_iokitInterface);
        for (;;) {
            const IOReturn ior = ((*_iokitInterface)->*T_Fn)(_iokitInterface, std::forward<T_Args>(args)...);
            if (ior != kIOReturnAborted) return ior;
        }
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
    
    USB::StringDescriptorMax stringDescriptor(uint8_t idx, uint16_t lang=USB::Language::English) const {
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
    
    template<typename... T_Args>
    auto read(uint8_t epAddr, T_Args&&... args) {
        const _EndpointInfo& epInfo = _epInfo(epAddr);
        _Interface& iface = _interfaces.at(epInfo.ifaceIdx);
        return iface.read(epInfo.pipeRef, std::forward<T_Args>(args)...);
    }
    
    template<typename... T_Args>
    void write(uint8_t epAddr, T_Args&&... args) {
        const _EndpointInfo& epInfo = _epInfo(epAddr);
        _Interface& iface = _interfaces.at(epInfo.ifaceIdx);
        iface.write(epInfo.pipeRef, std::forward<T_Args>(args)...);
    }
    
    template<typename... T_Args>
    void reset(uint8_t epAddr, T_Args&&... args) {
        const _EndpointInfo& epInfo = _epInfo(epAddr);
        _Interface& iface = _interfaces.at(epInfo.ifaceIdx);
        iface.reset(epInfo.pipeRef, std::forward<T_Args>(args)...);
    }
    
    template<typename T>
    void vendorRequestOut(uint8_t req, const T& x, Milliseconds timeout=Forever) {
        vendorRequestOut(req, (const void*)&x, sizeof(x), timeout);
    }
    
    void vendorRequestOut(uint8_t req, const void* data, size_t len, Milliseconds timeout=Forever) {
        IOUSBDevRequestTO usbReq = {
            .bmRequestType      = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice),
            .bRequest           = req,
            .pData              = (void*)data,
            .wLength            = (uint16_t)len,
            .noDataTimeout      = (uint32_t)0,
            .completionTimeout  = (uint32_t)timeout.count(),
        };
        
        IOReturn ior = iokitExec<&IOUSBDeviceInterface::DeviceRequestTO>(&usbReq);
        _CheckErr(ior, "DeviceRequestTO failed");
    }
    
    const SendRight& service() const { return _service; }
    
private:
    struct _EndpointInfo {
        bool valid = false;
        uint8_t epAddr = 0;
        uint8_t ifaceIdx = 0;
        uint8_t pipeRef = 0;
        uint16_t maxPacketSize = 0;
    };
    
    static uint8_t _IdxForEndpointAddr(uint8_t epAddr) {
        return ((epAddr&USB::Endpoint::DirectionMask)>>3) | (epAddr&USB::Endpoint::IndexMask);
    }
    
    static void _CheckErr(IOReturn ior, const char* errMsg) {
        if (ior != kIOReturnSuccess) throw RuntimeError("%s: %s", errMsg, mach_error_string(ior));
    }
    
    const _EndpointInfo& _epInfo(uint8_t epAddr) const {
        const _EndpointInfo& epInfo = _epInfos[_IdxForEndpointAddr(epAddr)];
        if (!epInfo.valid) throw RuntimeError("invalid endpoint address: 0x%02x", epAddr);
        return epInfo;
    }
    
//    _Interface& _interfaceForEndpointAddr(uint8_t epAddr) {
//        const uint8_t ifaceIdx = _ifaceIdxFromEp[_IdxForEndpointAddr(epAddr)];
//        return _interfaces.at(ifaceIdx);
//    }
//    
//    void _openIfNeeded() {
//        if (_open) return;
//        // Open the device
//        IOReturn ior = iokitExec<&IOUSBDeviceInterface::USBDeviceOpen>();
//        _CheckErr(ior, "USBDeviceOpen failed");
//        _open = true;
//    }
    
    SendRight _service;
    _IOUSBDeviceInterface _iokitInterface;
    std::vector<_Interface> _interfaces;
    _EndpointInfo _epInfos[USB::Endpoint::MaxCount];
//    bool _open = false;
    
#elif __linux__
    
    static std::vector<USBDevice> GetDevices() {
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
    void read(uint8_t epAddr, T_Dst& dst, Milliseconds timeout=Forever) {
        const size_t len = read(epAddr, (void*)&dst, sizeof(dst), timeout);
        if (len != sizeof(dst)) throw RuntimeError("read() didn't read enough data (needed %ju bytes, got %ju bytes)",
            (uintmax_t)sizeof(dst), (uintmax_t)len);
    }
    
    size_t read(uint8_t epAddr, void* buf, size_t len, Milliseconds timeout=Forever) {
        _claimInterfaceForEndpointAddr(epAddr);
        int xferLen = 0;
        int ir = libusb_bulk_transfer(_handle, epAddr, (uint8_t*)buf, (int)len, &xferLen,
            _LibUSBTimeoutFromMs(timeout));
        _CheckErr(ir, "libusb_bulk_transfer failed");
        return xferLen;
    }
    
    template<typename T_Src>
    void write(uint8_t epAddr, T_Src& src, Milliseconds timeout=Forever) {
        write(epAddr, (void*)&src, sizeof(src), timeout);
    }
    
    void write(uint8_t epAddr, const void* buf, size_t len, Milliseconds timeout=Forever) {
        _claimInterfaceForEndpointAddr(epAddr);
        
        int xferLen = 0;
        int ir = libusb_bulk_transfer(_handle, epAddr, (uint8_t*)buf, (int)len, &xferLen,
            _LibUSBTimeoutFromMs(timeout));
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
    void vendorRequestOut(uint8_t req, const T& x, Milliseconds timeout=Forever) {
        vendorRequestOut(req, (void*)&x, sizeof(x), timeout);
    }
    
    void vendorRequestOut(uint8_t req, const void* data, size_t len, Milliseconds timeout=Forever) {
        _openIfNeeded();
        
        const uint8_t bmRequestType =
            USB::RequestType::DirectionOut      |
            USB::RequestType::TypeVendor        |
            USB::RequestType::RecipientDevice   ;
        const uint8_t bRequest = req;
        const uint8_t wValue = 0;
        const uint8_t wIndex = 0;
        int ir = libusb_control_transfer(_handle, bmRequestType, bRequest, wValue, wIndex,
            (uint8_t*)data, len, _LibUSBTimeoutFromMs(timeout));
        _CheckErr(ir, "libusb_control_transfer failed");
    }
    
    operator libusb_device*() const { return _dev; }
    
private:
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
    
    static unsigned int _LibUSBTimeoutFromMs(Milliseconds timeout) {
        if (timeout == Forever) return 0;
        else if (timeout == Milliseconds::zero()) return 1;
        else return timeout.count();
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
    
public:
    
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
    
    std::vector<uint8_t> endpoints() {
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
