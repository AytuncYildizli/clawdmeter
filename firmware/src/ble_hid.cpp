#include "ble_hid.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

namespace ble_hid {

static NimBLEHIDDevice*    g_hid          = nullptr;
static NimBLECharacteristic* g_input       = nullptr;

// Standard boot keyboard report descriptor.
static const uint8_t REPORT_MAP[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224)
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute) — Modifier byte
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant) — Reserved
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array)
    0xC0         // End Collection
};

void begin() {
    NimBLEServer* server = NimBLEDevice::getServer();
    if (server == nullptr) {
        // ble_peer::begin() must run first; bail out quietly otherwise.
        return;
    }

    g_hid = new NimBLEHIDDevice(server);

    // Input report (report ID 1, 8-byte boot keyboard report) — host reads via notify.
    g_input = g_hid->getInputReport(1);

    // Manufacturer / pnp info — minimal but required for some hosts to bind HID.
    g_hid->setManufacturer("Clawd");
    // Vendor source = 0x02 (USB-IF), vid/pid/version are placeholders.
    g_hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);
    g_hid->setHidInfo(0x00, 0x01);

    g_hid->setReportMap(const_cast<uint8_t*>(REPORT_MAP), sizeof(REPORT_MAP));
    g_hid->startServices();

    // Advertise HID appearance so OS treats us as a keyboard. The advertising
    // started by ble_peer continues; we just add HID UUID + appearance.
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);  // Generic HID Keyboard
    adv->addServiceUUID(g_hid->getHidService()->getUUID());
    // Restart advertising so the new service UUID + appearance go on the air.
    adv->stop();
    adv->start();
}

void send_key(Modifier mod, Key key) {
    if (g_input == nullptr) return;

    uint8_t report[8] = {
        static_cast<uint8_t>(mod),
        0,
        static_cast<uint8_t>(key),
        0, 0, 0, 0, 0
    };
    g_input->setValue(report, sizeof(report));
    g_input->notify();

    // Release: all-zero report.
    uint8_t release[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    g_input->setValue(release, sizeof(release));
    g_input->notify();
}

}  // namespace ble_hid
