#pragma once

#include <NimBLEDevice.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "BleMidiParser.h"
#include "Analysis.h"

class BleMidiInput : private NimBLEServerCallbacks, private NimBLECharacteristicCallbacks,
                     private NimBLEScanCallbacks, private NimBLEClientCallbacks {
public:
    bool begin();
    void setMode(chord::BluetoothMode mode);
    void printStatus() const;
    void scanForDiagnostics();
    bool poll(chord::MidiState& state); // true when input recovery cleared notes
    bool connected() const { return connected_.load(); }
    uint32_t dropped() const { return dropped_.load(); }
    uint32_t malformed() const { return malformed_.load(); }
    uint32_t lastPacketMs() const { return lastPacketMs_.load(); }
    const std::string& address() const { return address_; }

private:
    struct Event { uint32_t generation; chord::MidiMessage message; };
    struct Peer { uint8_t address[6]; uint8_t type; char name[40]; };
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override;
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override;
    void onConnect(NimBLEClient*) override;
    void onDisconnect(NimBLEClient*, int reason) override;
    void onAuthenticationComplete(NimBLEConnInfo&) override;
    void onResult(const NimBLEAdvertisedDevice*) override;
    void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) override;
    void receivePacket(const uint8_t* bytes, size_t length);
    void serviceConnection();
    bool connectPeer(const Peer& peer);
    void disconnected();
    static void enqueue(void* context, chord::MidiMessage message);

    QueueHandle_t queue_ = nullptr;
    QueueHandle_t peers_ = nullptr;
    NimBLEClient* client_ = nullptr;
    chord::BleMidiParser parser_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> centralLink_{false};
    std::atomic<bool> advertisingNeeded_{false};
    std::atomic<uint32_t> generation_{0};
    std::atomic<uint32_t> dropped_{0};
    std::atomic<uint32_t> malformed_{0};
    std::atomic<uint32_t> lastPacketMs_{0};
    std::atomic<uint32_t> packets_{0};
    std::atomic<uint32_t> noteOns_{0};
    std::atomic<uint32_t> scanSeen_{0};
    std::atomic<chord::BluetoothMode> mode_{chord::BluetoothMode::Group};
    uint32_t consumedGeneration_ = 0;
    uint32_t lastSearchMs_ = 0;
    uint32_t lastAdvertisingAttemptMs_ = 0;
    unsigned loggedNotes_ = 0;
    std::string address_;
};
