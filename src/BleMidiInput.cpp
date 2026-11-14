#include "BleMidiInput.h"
#include <Arduino.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
constexpr char serviceUuid[] = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
constexpr char midiUuid[] = "7772e5db-3868-4112-a1a9-f2669d106bf3";
}

bool BleMidiInput::begin() {
    queue_ = xQueueCreate(256, sizeof(Event));
    peers_ = xQueueCreate(1, sizeof(Peer));
    if (!queue_ || !peers_ || !NimBLEDevice::init("Chord Display")) return false;
    // Keep the ESP32's public identity stable for WIDI's saved group address.
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setMTU(185);
    address_ = NimBLEDevice::getAddress().toString();

    auto* server = NimBLEDevice::createServer();
    server->setCallbacks(this, false);
    server->advertiseOnDisconnect(false); // the selected role controls advertising
    auto* service = server->createService(serviceUuid);
    auto* characteristic = service->createCharacteristic(midiUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    characteristic->setCallbacks(this);
    if (!server->start()) return false;

    auto* advertisement = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData data;
    data.setFlags(0x06);
    data.addServiceUUID(serviceUuid);
    data.setName("ChordDsp"); // keep a name in the primary 31-byte advertisement
    NimBLEAdvertisementData scan;
    scan.setName("Chord Display");
    if (!advertisement->setAdvertisementData(data) || !advertisement->setScanResponseData(scan)) return false;
    advertisement->enableScanResponse(true);
    advertisement->setMinInterval(32); // 20 ms
    advertisement->setMaxInterval(64); // 40 ms
    auto* scanner = NimBLEDevice::getScan();
    scanner->setScanCallbacks(this);
    scanner->setActiveScan(true);
    scanner->setInterval(100);
    scanner->setWindow(60);
    scanner->setScanResponseTimeout(500);
    scanner->setMaxResults(0); // copy candidate addresses; do not retain scan objects
    lastSearchMs_ = millis();
    Serial.printf("BLE MIDI: Chord Display [%s]; mode=%u (0=auto 1=group 2=direct)\n", address_.c_str(), unsigned(mode_.load()));
    return mode_.load() == chord::BluetoothMode::Direct || advertisement->start();
}

void BleMidiInput::onConnect(NimBLEServer* server, NimBLEConnInfo& info) {
    parser_.reset();
    generation_.fetch_add(1);
    centralLink_.store(false);
    advertisingNeeded_.store(false);
    connected_.store(true);
    NimBLEDevice::getScan()->stop();
    xQueueReset(peers_);
    server->updateConnParams(info.getConnHandle(), 6, 12, 0, 400);
    Serial.printf("BLE peripheral connected to central: %s\n", info.getAddress().toString().c_str());
}

void BleMidiInput::onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) {
    disconnected();
    Serial.printf("BLE peripheral disconnected (%d); retrying\n", reason);
}

void BleMidiInput::onConnect(NimBLEClient* client) {
    parser_.reset();
    generation_.fetch_add(1);
    centralLink_.store(true);
    advertisingNeeded_.store(false);
    Serial.printf("BLE central link established: %s; discovering MIDI service\n", client->getPeerAddress().toString().c_str());
}

void BleMidiInput::onDisconnect(NimBLEClient*, int reason) {
    disconnected();
    Serial.printf("BLE central disconnected (%d); retrying\n", reason);
}

void BleMidiInput::disconnected() {
    connected_.store(false);
    centralLink_.store(false);
    parser_.reset();
    generation_.fetch_add(1);
    advertisingNeeded_.store(true);
}

void BleMidiInput::onAuthenticationComplete(NimBLEConnInfo& info) {
    Serial.printf("BLE authentication: encrypted=%d bonded=%d\n", info.isEncrypted(), info.isBonded());
}

void BleMidiInput::onResult(const NimBLEAdvertisedDevice* device) {
    scanSeen_.fetch_add(1);
    if (connected_.load()) return;
    auto name = device->getName();
    auto lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    const bool widi = lower.find("widi") != std::string::npos;
    const bool midi = device->isAdvertisingService(NimBLEUUID(serviceUuid));
    if (!widi && !midi) return;
    if (lower.find("chord display") != std::string::npos) return;
    Serial.printf("BLE MIDI discovered: %s [%s], RSSI %d, service=%d\n",
        name.c_str(), device->getAddress().toString().c_str(), device->getRSSI(), midi);
    if (mode_.load() == chord::BluetoothMode::Group) return;
    // Auto mode is deliberately limited to WIDI devices; Direct also supports
    // renamed devices that advertise the standard BLE MIDI service.
    if (!widi && mode_.load() != chord::BluetoothMode::Direct) return;
    Peer peer{};
    const auto address = device->getAddress();
    std::memcpy(peer.address, address.getVal(), sizeof(peer.address));
    peer.type = address.getType();
    std::strncpy(peer.name, name.c_str(), sizeof(peer.name) - 1);
    if (xQueueSend(peers_, &peer, 0) == pdTRUE) NimBLEDevice::getScan()->stop();
}

void BleMidiInput::setMode(chord::BluetoothMode mode) {
    if (mode_.exchange(mode) == mode || !queue_) return;
    NimBLEDevice::getScan()->stop();
    xQueueReset(peers_);
    lastSearchMs_ = millis();
    advertisingNeeded_.store(mode != chord::BluetoothMode::Direct);
    auto* server = NimBLEDevice::getServer();
    if (mode == chord::BluetoothMode::Direct && connected_.load() && !centralLink_.load()) {
        for (auto handle : server->getPeerDevices()) server->disconnect(handle);
    } else if (mode == chord::BluetoothMode::Group && client_ && client_->isConnected()) client_->disconnect();
    if (mode == chord::BluetoothMode::Direct) NimBLEDevice::getAdvertising()->stop();
}

bool BleMidiInput::connectPeer(const Peer& peer) {
    const NimBLEAddress address(peer.address, peer.type);
    Serial.printf("Connecting to %s [%s] as BLE central\n", peer.name, address.toString().c_str());
    if (!client_) {
        client_ = NimBLEDevice::createClient();
        if (!client_) return false;
        client_->setClientCallbacks(this, false);
        client_->setConnectTimeout(4000);
        client_->setConnectRetries(0);
        client_->setConnectionParams(6, 12, 0, 400);
    }
    if (!client_->connect(address)) {
        Serial.printf("BLE connection failed: error=%d\n", client_->getLastError());
        return false;
    }
    auto* service = client_->getService(serviceUuid);
    auto* characteristic = service ? service->getCharacteristic(midiUuid) : nullptr;
    if (!characteristic || !characteristic->canNotify()) {
        Serial.println("Peer has no BLE MIDI notification characteristic");
        client_->disconnect();
        return false;
    }
    auto receive = [this](NimBLERemoteCharacteristic*, uint8_t* bytes, size_t length, bool) { receivePacket(bytes, length); };
    bool subscribed = characteristic->subscribe(true, receive);
    if (!subscribed && client_->isConnected()) {
        Serial.printf("MIDI subscribe failed (%d); retrying with encryption\n", client_->getLastError());
        subscribed = client_->secureConnection() && characteristic->subscribe(true, receive);
    }
    if (!subscribed || !client_->isConnected()) {
        Serial.printf("BLE MIDI subscription failed: error=%d\n", client_->getLastError());
        client_->disconnect();
        return false;
    }
    connected_.store(true);
    Serial.printf("BLE MIDI READY: %s [%s], notifications subscribed, MTU %u\n",
        peer.name, address.toString().c_str(), client_->getMTU());
    return true;
}

void BleMidiInput::serviceConnection() {
    if (connected_.load()) return;
    auto* scanner = NimBLEDevice::getScan();
    auto* advertisement = NimBLEDevice::getAdvertising();
    const auto mode = mode_.load();
    if (NimBLEDevice::getServer()->getConnectedCount()) return;
    if (client_ && client_->isConnected()) return; // a failed handshake is closing
    Peer peer{};
    if (mode != chord::BluetoothMode::Group && xQueueReceive(peers_, &peer, 0) == pdTRUE) {
        scanner->stop();
        advertisement->stop();
        advertisingNeeded_.store(false);
        if (!connectPeer(peer)) advertisingNeeded_.store(true);
        lastSearchMs_ = millis();
        return;
    }
    // Advertising also stops briefly before the connect callback arrives.
    // Restart only after an actual disconnect/mode change, not on every loop.
    if (mode != chord::BluetoothMode::Direct && advertisingNeeded_.load()) {
        if (advertisement->isAdvertising()) advertisingNeeded_.store(false);
        else if (uint32_t(millis() - lastAdvertisingAttemptMs_) >= 500) {
            lastAdvertisingAttemptMs_ = millis();
            if (advertisement->start()) advertisingNeeded_.store(false);
        }
    }
    const uint32_t wait = mode == chord::BluetoothMode::Auto ? 6000 : 1000;
    if (mode != chord::BluetoothMode::Group && !scanner->isScanning() && uint32_t(millis() - lastSearchMs_) >= wait) {
        lastSearchMs_ = millis();
        Serial.println("Scanning for WIDI / BLE MIDI peripherals...");
        scanner->start(5000);
    }
}

void BleMidiInput::printStatus() const {
    Serial.printf("BLE status: mode=%u central=%d connected=%d scanning=%d advertising=%d seen=%lu packets=%lu note-ons=%lu dropped=%lu bad=%lu\n",
        unsigned(mode_.load()), centralLink_.load(), connected_.load(), NimBLEDevice::getScan()->isScanning(),
        NimBLEDevice::getAdvertising()->isAdvertising(), static_cast<unsigned long>(scanSeen_.load()), static_cast<unsigned long>(packets_.load()),
        static_cast<unsigned long>(noteOns_.load()), static_cast<unsigned long>(dropped_.load()),
        static_cast<unsigned long>(malformed_.load()));
}

void BleMidiInput::scanForDiagnostics() {
    if (connected_.load()) { Serial.println("Already connected; leaving the MIDI connection intact"); return; }
    Serial.println("Diagnostic BLE scan (does not initiate connections in Group mode)");
    NimBLEDevice::getScan()->start(5000);
}

void BleMidiInput::enqueue(void* context, chord::MidiMessage message) {
    // Timing/active-sensing traffic does not consume the display's event queue.
    if (message.status >= 0xF0 && message.status != 0xFF) return;
    if (message.status < 0xF0 && (message.status & 0xF0) != 0x80 &&
        (message.status & 0xF0) != 0x90 && (message.status & 0xF0) != 0xB0) return;
    auto& input = *static_cast<BleMidiInput*>(context);
    if ((message.status & 0xF0) == 0x90 && message.data2) input.noteOns_.fetch_add(1);
    const Event event{input.generation_.load(), message};
    if (xQueueSend(input.queue_, &event, 0) != pdTRUE) {
        input.dropped_.fetch_add(1);
        input.generation_.fetch_add(1); // invalidate all queued pre-overrun notes
    }
}

void BleMidiInput::onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) {
    const auto& value = characteristic->getValue();
    receivePacket(value.data(), value.size());
}

void BleMidiInput::receivePacket(const uint8_t* bytes, size_t length) {
    if (!length) return;
    packets_.fetch_add(1);
    lastPacketMs_.store(millis());
    if (!parser_.parse(bytes, length, enqueue, this)) {
        malformed_.fetch_add(1);
        parser_.reset();
        generation_.fetch_add(1); // a corrupt release must not leave a held note
    }
}

bool BleMidiInput::poll(chord::MidiState& state) {
    serviceConnection();
    bool recovered = false;
    auto sync = [&]() {
        const uint32_t current = generation_.load();
        if (current != consumedGeneration_) {
            state.clear();
            consumedGeneration_ = current;
            recovered = true;
        }
    };
    sync();
    Event event{};
    // A bounded drain keeps touch handling responsive during a dense stream.
    for (int i = 0; i < 256 && xQueueReceive(queue_, &event, 0) == pdTRUE; ++i) {
        sync();
        if (event.generation == consumedGeneration_) {
            state.receive(event.message);
            if ((event.message.status & 0xF0) == 0x90 && event.message.data2 && loggedNotes_ < 8) {
                ++loggedNotes_;
                Serial.printf("MIDI note-on: channel=%u note=%u velocity=%u\n",
                    (event.message.status & 15) + 1, event.message.data1, event.message.data2);
            }
        }
    }
    sync();
    return recovered;
}
