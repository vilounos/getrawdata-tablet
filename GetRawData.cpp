#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <thread>
#include <atomic>
#include <fstream>
#include <chrono>
#include <sstream>
#include <limits>
#include <ios>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

struct DeviceInfo {
    std::wstring devicePath;
    std::wstring productName;
    std::wstring manufacturerName;
    USHORT vendorId;
    USHORT productId;
    USHORT usagePage;
    USHORT usage;
};

class PeripheralReader {
private:
    std::vector<DeviceInfo> devices;
    std::atomic<bool> stopReading{ false };
    std::wofstream logFile;
    std::wstring tabletName;

    std::wstring GetCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::wstringstream ss;
        std::tm* timeinfo = std::localtime(&time_t);
        ss << std::put_time(timeinfo, L"%Y-%m-%d %H:%M:%S");
        ss << L"." << std::setfill(L'0') << std::setw(3) << ms.count();
        return ss.str();
    }

    void LogMessage(const std::wstring& message) {
        std::wstring timestamp = GetCurrentTimestamp();
        std::wstring logEntry = timestamp + L" - " + message + L"\n";

        std::wcout << logEntry;
        if (logFile.is_open()) {
            logFile << logEntry;
            logFile.flush();
        }
    }

    void WriteDeviceInfo(const DeviceInfo& device) {
        if (logFile.is_open()) {
            logFile << L"DEVICE_INFO|" << tabletName << L"|" << device.productName
                << L"|VID:0x" << std::hex << std::uppercase << device.vendorId
                << L"|PID:0x" << device.productId << std::dec << L"\n";
            logFile.flush();
        }
    }

    void ClearConsole() {
        system("cls");
    }

    void WriteTabletData(const std::wstring& deviceName, int phase, int reportNum, const std::vector<BYTE>& data, DWORD bytesRead) {
        if (logFile.is_open()) {
            logFile << tabletName << L"|" << deviceName << L"|" << phase << L"|" << reportNum << L"|";

            if (bytesRead > 0) {
                for (DWORD i = 0; i < bytesRead; i++) {
                    logFile << std::hex << std::setw(2) << std::setfill(L'0')
                        << static_cast<int>(data[i]);
                    if (i < bytesRead - 1) logFile << L" ";
                }
            }
            else {
                logFile << L"NO_DATA";
            }
            logFile << std::dec << L"\n";
            logFile.flush();
        }
    }

    void WaitForEnter(const std::wstring& prompt) {
        std::wcout << prompt << L" (Press Enter to continue...)";
        std::wcin.clear();
        std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');
        std::wcin.get();
    }

public:
    bool InitializeDataFile() {
        logFile.open(L"rawdata.txt", std::ios::out | std::ios::trunc);
        if (!logFile.is_open()) {
            std::wcout << L"Failed to create rawdata.txt file\n";
            return false;
        }
        return true;
    }

    void SetTabletName(const std::wstring& name) {
        tabletName = name;
    }

    bool EnumerateDevices() {
        devices.clear();

        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);

        HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
            &hidGuid,
            NULL,
            NULL,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        );

        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            LogMessage(L"Failed to get device info set");
            return false;
        }

        SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
        deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &hidGuid, i, &deviceInterfaceData); i++) {
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, NULL, 0, &requiredSize, NULL);

            if (requiredSize == 0) continue;

            PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData =
                (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
            deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData,
                deviceInterfaceDetailData, requiredSize, &requiredSize, NULL)) {

                HANDLE deviceHandle = CreateFile(
                    deviceInterfaceDetailData->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL
                );

                if (deviceHandle != INVALID_HANDLE_VALUE) {
                    DeviceInfo info;
                    info.devicePath = deviceInterfaceDetailData->DevicePath;

                    HIDD_ATTRIBUTES attributes;
                    attributes.Size = sizeof(HIDD_ATTRIBUTES);

                    if (HidD_GetAttributes(deviceHandle, &attributes)) {
                        info.vendorId = attributes.VendorID;
                        info.productId = attributes.ProductID;
                    }

                    PHIDP_PREPARSED_DATA preparsedData;
                    if (HidD_GetPreparsedData(deviceHandle, &preparsedData)) {
                        HIDP_CAPS caps;
                        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
                            info.usagePage = caps.UsagePage;
                            info.usage = caps.Usage;
                        }
                        HidD_FreePreparsedData(preparsedData);
                    }

                    wchar_t buffer[256];
                    if (HidD_GetProductString(deviceHandle, buffer, sizeof(buffer))) {
                        info.productName = buffer;
                    }
                    else {
                        info.productName = L"Unknown Product";
                    }

                    if (HidD_GetManufacturerString(deviceHandle, buffer, sizeof(buffer))) {
                        info.manufacturerName = buffer;
                    }
                    else {
                        info.manufacturerName = L"Unknown Manufacturer";
                    }

                    devices.push_back(info);
                    CloseHandle(deviceHandle);
                }
            }

            free(deviceInterfaceDetailData);
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);

        return !devices.empty();
    }

    void DisplayDevices() {
        std::wcout << L"\n=== Available Peripherals ===\n";

        for (size_t i = 0; i < devices.size(); i++) {
            const auto& device = devices[i];
            std::wstringstream deviceInfo;
            deviceInfo << L"[" << i + 1 << L"] " << device.manufacturerName
                << L" - " << device.productName
                << L" (VID: 0x" << std::hex << std::uppercase << device.vendorId
                << L" PID: 0x" << device.productId << std::dec
                << L" Usage Page: 0x" << std::hex << device.usagePage
                << L" Usage: 0x" << device.usage << std::dec << L")";

            std::wcout << deviceInfo.str() << L"\n\n";
        }
    }

    std::wstring GetUsageDescription(USHORT usagePage, USHORT usage) {
        if (usagePage == 0x01) {
            switch (usage) {
            case 0x02: return L"Mouse";
            case 0x06: return L"Keyboard";
            case 0x04: return L"Joystick";
            case 0x05: return L"Game Pad";
            case 0x08: return L"Multi-axis Controller";
            default: return L"Generic Desktop Device";
            }
        }
        else if (usagePage == 0x0C) {
            return L"Consumer Control Device";
        }
        else if (usagePage == 0x0D) {
            return L"Digitizer/Touch";
        }
        return L"Unknown Device Type";
    }

    void ReadDeviceDataWithCheckpoints(size_t deviceIndex) {
        if (deviceIndex >= devices.size()) {
            LogMessage(L"Invalid device index: " + std::to_wstring(deviceIndex));
            return;
        }

        const auto& device = devices[deviceIndex];

        WriteDeviceInfo(device);

        HANDLE deviceHandle = CreateFile(
            device.devicePath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (deviceHandle == INVALID_HANDLE_VALUE) {
            LogMessage(L"Failed to open device: " + std::to_wstring(GetLastError()));
            return;
        }

        PHIDP_PREPARSED_DATA preparsedData;
        if (!HidD_GetPreparsedData(deviceHandle, &preparsedData)) {
            LogMessage(L"Failed to get preparsed data");
            CloseHandle(deviceHandle);
            return;
        }

        HIDP_CAPS caps;
        if (HidP_GetCaps(preparsedData, &caps) != HIDP_STATUS_SUCCESS) {
            LogMessage(L"Failed to get device capabilities");
            HidD_FreePreparsedData(preparsedData);
            CloseHandle(deviceHandle);
            return;
        }

        LogMessage(L"=== Tablet Information ===");
        LogMessage(L"Device: " + device.productName + L" (" + tabletName + L")");
        LogMessage(L"Vendor ID: " + std::to_wstring(device.vendorId));
        LogMessage(L"Product ID: " + std::to_wstring(device.productId));
        LogMessage(L"Device Type: " + GetUsageDescription(device.usagePage, device.usage));
        LogMessage(L"Input Report Length: " + std::to_wstring(caps.InputReportByteLength) + L" bytes");

        std::vector<BYTE> buffer(caps.InputReportByteLength);

        struct Phase {
            int number;
            std::wstring name;
            std::wstring instruction;
        };

        std::vector<Phase> phases = {
            {1, L"BASELINE", L"Phase 1: Keep the pen away from the tablet (no contact, no hover)"},
            {2, L"HOVER", L"Phase 2: Move the pen above the tablet surface (hover without touching)"},
            {3, L"LIGHT_TOUCH", L"Phase 3: Gently touch the tablet with the pen (light pressure)"},
            {4, L"HEAVY_PRESSURE", L"Phase 4: Press the pen firmly against the tablet (heavy pressure)"}
        };

        for (const auto& phase : phases) {
            LogMessage(L"");
            LogMessage(L"=== CHECKPOINT PHASE " + std::to_wstring(phase.number) + L": " + phase.name + L" ===");
            LogMessage(L"Instruction: " + phase.instruction);

            WaitForEnter(L"Prepare for " + phase.name + L" phase");

            std::wcout << L"Starting in 1 second...\n";
            Sleep(1000);


            LogMessage(L"Phase " + std::to_wstring(phase.number) + L" data collection started");

            DWORD bytesRead;
            int sampleCount = 0;
            const int maxSamples = 5;

            for (int sample = 1; sample <= maxSamples; sample++) {
                std::wcout << L"Collecting sample " << sample << L"/5...\n";

                bool sampleCollected = false;
                auto sampleStartTime = std::chrono::steady_clock::now();
                auto sampleEndTime = sampleStartTime + std::chrono::seconds(1);

                while (std::chrono::steady_clock::now() < sampleEndTime && !sampleCollected) {
                    if (ReadFile(deviceHandle, buffer.data(), caps.InputReportByteLength, &bytesRead, NULL)) {
                        if (bytesRead > 0) {
                            sampleCount++;
                            sampleCollected = true;

                            std::wcout << L"Sample " << sample << L" collected: ";
                            for (DWORD i = 0; i < bytesRead; i++) {
                                std::wcout << L"0x" << std::hex << std::setw(2) << std::setfill(L'0')
                                    << static_cast<int>(buffer[i]) << L" ";
                            }
                            std::wcout << std::dec << L"\n";

                            WriteTabletData(device.productName, phase.number, sample, buffer, bytesRead);
                        }
                    }
                    else {
                        DWORD error = GetLastError();
                        if (error != ERROR_IO_PENDING) {
                            LogMessage(L"Read error in phase " + std::to_wstring(phase.number) + L", sample " + std::to_wstring(sample) + L": " + std::to_wstring(error));
                            break;
                        }
                    }
                    Sleep(10);
                }

                if (!sampleCollected) {
                    std::wcout << L"No data received for sample " << sample << L"\n";
                    WriteTabletData(device.productName, phase.number, sample, std::vector<BYTE>(), 0);
                }

                while (std::chrono::steady_clock::now() < sampleEndTime) {
                    Sleep(10);
                }
            }

            LogMessage(L"Phase " + std::to_wstring(phase.number) + L" completed. Samples collected: " + std::to_wstring(sampleCount));

            Sleep(1000);
            ClearConsole();
        }

        std::wcout << L"\n=== ALL PHASES COMPLETED ===\n";
        std::wcout << L"Tablet data saved to rawdata.txt\n";
        std::wcout << L"Press Enter to exit...";
        std::wcin.get();

        HidD_FreePreparsedData(preparsedData);
        CloseHandle(deviceHandle);
    }

    ~PeripheralReader() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }
};

int main() {
    std::wcout << L"Windows Peripheral Raw Data Reader with Checkpoints\n";
    std::wcout << L"===================================================\n";

    PeripheralReader reader;

    if (!reader.InitializeDataFile()) {
        std::wcout << L"Failed to initialize data file. Press Enter to exit...";
        std::wcin.get();
        return 1;
    }

    std::wcout << L"Enter the name of your tablet: ";
    std::wstring tabletName;
    std::getline(std::wcin, tabletName);
    reader.SetTabletName(tabletName);

    std::wcout << L"Enumerating HID devices...\n";
    if (!reader.EnumerateDevices()) {
        std::wcout << L"No HID devices found or enumeration failed.\n";
        std::wcout << L"Press Enter to exit...";
        std::wcin.get();
        return 1;
    }

    while (true) {
        reader.DisplayDevices();

        std::wcout << L"Enter device number to start checkpoint testing (0 to refresh, -1 to exit): ";
        int choice;
        std::wcin >> choice;

        if (choice == -1) {
            break;
        }
        else if (choice == 0) {
            std::wcout << L"Refreshing device list...\n";
            reader.EnumerateDevices();
            continue;
        }
        else if (choice > 0) {
            reader.ReadDeviceDataWithCheckpoints(choice - 1);
            break;
        }
        else {
            std::wcout << L"Invalid choice.\n";
        }

        std::wcout << L"\n";
    }

    std::wcout << L"Goodbye!\n";
    return 0;
}