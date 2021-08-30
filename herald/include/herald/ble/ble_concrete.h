//  Copyright 2020-2021 Herald Project Contributors
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef BLE_CONCRETE_H
#define BLE_CONCRETE_H

#include "ble_database.h"
#include "ble_receiver.h"
#include "ble_sensor.h"
#include "ble_transmitter.h"
#include "ble_protocols.h"
#include "bluetooth_state_manager.h"
#include "ble_device_delegate.h"
#include "../payload/payload_data_supplier.h"
#include "../context.h"

#include <memory>

namespace herald {
namespace ble {

using namespace herald::datatype;
using namespace herald::payload;

// NOTE THIS HEADER IS FOR ALL PLATFORMS. 
//      SPECIFIC PLATFORM DEFINITIONS ARE WITHIN SEVERAL C++ FILES
//      UNDER WINDOWS AND ZEPHYR SUB DIRECTORIES

class ConcreteBluetoothStateManager : public BluetoothStateManager, public std::enable_shared_from_this<BluetoothStateManager>  {
public:
  ConcreteBluetoothStateManager();
  ConcreteBluetoothStateManager(const ConcreteBluetoothStateManager& from) = delete;
  ConcreteBluetoothStateManager(ConcreteBluetoothStateManager&& from) = delete;
  ~ConcreteBluetoothStateManager();

  // Bluetooth State Manager overrides
  void add(std::shared_ptr<BluetoothStateManagerDelegate> delegate) override;
  BluetoothState state() override;
};

template <typename ContextT>
class ConcreteBLEDatabase : public BLEDatabase, public BLEDeviceDelegate, public std::enable_shared_from_this<ConcreteBLEDatabase<ContextT>>  {
public:
  ConcreteBLEDatabase(ContextT& context);
  ConcreteBLEDatabase(const ConcreteBLEDatabase& from) = delete;
  ConcreteBLEDatabase(ConcreteBLEDatabase&& from) = delete;
  ~ConcreteBLEDatabase();

  // BLE Database overrides

  void add(const std::shared_ptr<BLEDatabaseDelegate>& delegate) override;

  // Creation overrides
  std::shared_ptr<BLEDevice> device(const BLEMacAddress& mac, const Data& advert/*, const RSSI& rssi*/) override;
  std::shared_ptr<BLEDevice> device(const BLEMacAddress& mac, const BLEMacAddress& pseudo) override;
  std::shared_ptr<BLEDevice> device(const BLEMacAddress& mac) override;
  std::shared_ptr<BLEDevice> device(const PayloadData& payloadData) override;
  std::shared_ptr<BLEDevice> device(const TargetIdentifier& targetIdentifier) override;
  
  // Introspection overrides
  std::size_t size() const override;

  std::vector<std::shared_ptr<BLEDevice>> matches(
    const std::function<bool(std::shared_ptr<BLEDevice>&)>& matcher) const override;

  // std::vector<std::shared_ptr<BLEDevice>> devices() const override;

  /// Cannot name a function delete in C++. remove is common.
  void remove(const TargetIdentifier& targetIdentifier) override;

  // std::optional<PayloadSharingData> payloadSharingData(const std::shared_ptr<BLEDevice>& peer) override;

  // BLE Device Delegate overrides
  void device(const std::shared_ptr<BLEDevice>& device, BLEDeviceAttribute didUpdate) override;

private:
  class Impl;
  std::unique_ptr<Impl> mImpl; // unique as this is handled internally for all platforms by Herald
};

/**
 * Acts as the main object to control the receiver, transmitter, and database instances
 */
template <typename ContextT>
class ConcreteBLESensor : public BLESensor, public BLEDatabaseDelegate, 
  public BluetoothStateManagerDelegate, public std::enable_shared_from_this<ConcreteBLESensor<ContextT>>  {
public:
  ConcreteBLESensor(ContextT& ctx, BluetoothStateManager& bluetoothStateManager, 
    std::shared_ptr<PayloadDataSupplier> payloadDataSupplier);
  ConcreteBLESensor(const ConcreteBLESensor& from) = delete;
  ConcreteBLESensor(ConcreteBLESensor&& from) = delete;
  ~ConcreteBLESensor();

  // Coordination overrides - Since v1.2-beta3
  std::optional<std::shared_ptr<CoordinationProvider>> coordinationProvider() override;

  bool immediateSend(Data data, const TargetIdentifier& targetIdentifier);
  bool immediateSendAll(Data data);

  // Sensor overrides
  void add(const std::shared_ptr<SensorDelegate>& delegate) override;
  void start() override;
  void stop() override;

  // Database overrides
  void bleDatabaseDidCreate(const std::shared_ptr<BLEDevice>& device) override;
  void bleDatabaseDidUpdate(const std::shared_ptr<BLEDevice>& device, const BLEDeviceAttribute attribute) override;
  void bleDatabaseDidDelete(const std::shared_ptr<BLEDevice>& device) override;

  // Bluetooth state manager delegate overrides
  void bluetoothStateManager(BluetoothState didUpdateState) override;

private:
  class Impl;
  std::unique_ptr<Impl> mImpl; // unique as this is handled internally for all platforms by Herald
};

template <typename ContextT>
class ConcreteBLEReceiver : public BLEReceiver, public HeraldProtocolV1Provider, public std::enable_shared_from_this<ConcreteBLEReceiver<ContextT>> {
public:
  ConcreteBLEReceiver(ContextT& ctx, BluetoothStateManager& bluetoothStateManager, 
    std::shared_ptr<PayloadDataSupplier> payloadDataSupplier, std::shared_ptr<BLEDatabase> bleDatabase);
  ConcreteBLEReceiver(const ConcreteBLEReceiver& from) = delete;
  ConcreteBLEReceiver(ConcreteBLEReceiver&& from) = delete;
  ~ConcreteBLEReceiver();

  // Coordination overrides - Since v1.2-beta3
  std::optional<std::shared_ptr<CoordinationProvider>> coordinationProvider() override;

  bool immediateSend(Data data, const TargetIdentifier& targetIdentifier) override;
  bool immediateSendAll(Data data) override;

  // Sensor overrides
  void add(const std::shared_ptr<SensorDelegate>& delegate) override;
  void start() override;
  void stop() override;

  // Herald V1 protocol provider overrides
  // C++17 CALLBACK VERSION:-
  // void openConnection(const TargetIdentifier& toTarget, const HeraldConnectionCallback& connCallback) override;
  // void closeConnection(const TargetIdentifier& toTarget, const HeraldConnectionCallback& connCallback) override;
  // void serviceDiscovery(Activity, CompletionCallback) override;
  // void readPayload(Activity, CompletionCallback) override;
  // void immediateSend(Activity, CompletionCallback) override;
  // void immediateSendAll(Activity, CompletionCallback) override;
  
  // NON C++17 VERSION:-
  bool openConnection(const TargetIdentifier& toTarget) override;
  bool closeConnection(const TargetIdentifier& toTarget) override;
  void restartScanningAndAdvertising() override;
  std::optional<Activity> serviceDiscovery(Activity) override;
  std::optional<Activity> readPayload(Activity) override;
  std::optional<Activity> immediateSend(Activity) override;
  std::optional<Activity> immediateSendAll(Activity) override;

private:
  class Impl;
  std::shared_ptr<Impl> mImpl; // shared to allow static callbacks to be bound
};

template <typename ContextT>
class ConcreteBLETransmitter : public BLETransmitter, public std::enable_shared_from_this<ConcreteBLETransmitter<ContextT>> {
public:
  ConcreteBLETransmitter(ContextT& ctx, BluetoothStateManager& bluetoothStateManager, 
    std::shared_ptr<PayloadDataSupplier> payloadDataSupplier, std::shared_ptr<BLEDatabase> bleDatabase);
  ConcreteBLETransmitter(const ConcreteBLETransmitter& from) = delete;
  ConcreteBLETransmitter(ConcreteBLETransmitter&& from) = delete;
  ~ConcreteBLETransmitter();

  // Coordination overrides - Since v1.2-beta3
  std::optional<std::shared_ptr<CoordinationProvider>> coordinationProvider() override;

  // Sensor overrides
  void add(const std::shared_ptr<SensorDelegate>& delegate) override;
  void start() override;
  void stop() override;

private:
  class Impl;
  std::shared_ptr<Impl> mImpl; // shared to allow static callbacks to be bound
};

} // end namespace
} // end namespace

#endif