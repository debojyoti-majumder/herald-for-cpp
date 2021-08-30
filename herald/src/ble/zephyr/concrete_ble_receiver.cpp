//  Copyright 2020-2021 Herald Project Contributors
//  SPDX-License-Identifier: Apache-2.0
//

#include "herald/zephyr_context.h"
#include "herald/data/sensor_logger.h"
#include "herald/ble/zephyr/concrete_ble_receiver.h"
#include "herald/ble/ble_concrete.h"
#include "herald/ble/ble_database.h"
#include "herald/ble/ble_receiver.h"
#include "herald/ble/ble_sensor.h"
#include "herald/ble/ble_sensor_configuration.h"
#include "herald/ble/bluetooth_state_manager.h"
#include "herald/datatype/data.h"
#include "herald/datatype/payload_data.h"
#include "herald/datatype/immediate_send_data.h"
#include "herald/ble/ble_mac_address.h"

// nRF Connect SDK includes
#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/addr.h>
#include <bluetooth/scan.h>
#include <kernel.h>
#include <zephyr/types.h>
#include <zephyr.h>

// C++17 includes
#include <memory>
#include <vector>
#include <map>

namespace herald {
namespace ble {

using namespace herald::datatype;
using namespace herald::data;

// ZEPHYR UTILITY FUNCTIONS
/** wait with timeout for Zephyr. Returns true if the function timed out rather than completed **/
uint32_t waitWithTimeout(uint32_t timeoutMillis, k_timeout_t period, std::function<bool()> keepWaiting)
{
  uint32_t start_time;
  uint32_t stop_time;
  uint32_t millis_spent;
  bool notComplete = keepWaiting();
  if (!notComplete) {
    return 0;
  }
  /* capture initial time stamp */
  start_time = k_uptime_get_32();

  /* capture final time stamp */
  stop_time = k_uptime_get_32();
  /* compute how long the work took (assumes no counter rollover) */
  millis_spent = stop_time - start_time;

  while (millis_spent < timeoutMillis && notComplete) {
    k_sleep(period);
    notComplete = keepWaiting();

    /* capture final time stamp */
    stop_time = k_uptime_get_32();
    /* compute how long the work took (assumes no counter rollover) */
    millis_spent = stop_time - start_time;
  }
  if (notComplete) {
    return millis_spent;
  } else {
    return 0;
  }
}


struct ConnectedDeviceState {
  ConnectedDeviceState(const TargetIdentifier& id)
    : target(id), state(BLEDeviceState::disconnected), connection(NULL), address(),
      readPayload(), immediateSend(), remoteInstigated(false)
  {}
  ConnectedDeviceState(const ConnectedDeviceState& from) = delete;
  ConnectedDeviceState(ConnectedDeviceState&& from) = delete;
  ~ConnectedDeviceState() = default;

  TargetIdentifier target;
  BLEDeviceState state;
  bt_conn* connection;
  bt_addr_le_t address;
  PayloadData readPayload;
  ImmediateSendData immediateSend;
  bool remoteInstigated;
};


// struct AddrRef {
//   const bt_addr_le_t* addr;
// };

namespace zephyrinternal {
  
  /* Herald Service Variables */
  static struct bt_uuid_128 herald_uuid = BT_UUID_INIT_128(
    0x9b, 0xfd, 0x5b, 0xd6, 0x72, 0x45, 0x1e, 0x80, 0xd3, 0x42, 0x46, 0x47, 0xaf, 0x32, 0x81, 0x42
  );
  static struct bt_uuid_128 herald_char_signal_android_uuid = BT_UUID_INIT_128(
    0x11, 0x1a, 0x82, 0x80, 0x9a, 0xe0, 0x24, 0x83, 0x7a, 0x43, 0x2e, 0x09, 0x13, 0xb8, 0x17, 0xf6
  );
  static struct bt_uuid_128 herald_char_signal_ios_uuid = BT_UUID_INIT_128(
    0x63, 0x43, 0x2d, 0xb0, 0xad, 0xa4, 0xf3, 0x8a, 0x9a, 0x4a, 0xe4, 0xea, 0xf2, 0xd5, 0xb0, 0x0e
  );
  static struct bt_uuid_128 herald_char_payload_uuid = BT_UUID_INIT_128(
    0xe7, 0x33, 0x89, 0x8f, 0xe3, 0x43, 0x21, 0xa1, 0x29, 0x48, 0x05, 0x8f, 0xf8, 0xc0, 0x98, 0x3e
  );
  

  bt_le_conn_param* BTLEConnParam = BT_LE_CONN_PARAM_DEFAULT; // BT_LE_CONN_PARAM(0x018,3200,0,400); // NOT BT_LE_CONN_PARAM_DEFAULT;
  bt_conn_le_create_param* BTLECreateParam = BT_CONN_LE_CREATE_CONN; // BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_NONE, 0x0010,0x0010);// NOT BT_CONN_LE_CREATE_CONN;

  static struct bt_conn_le_create_param defaultCreateParam = BT_CONN_LE_CREATE_PARAM_INIT(
    BT_CONN_LE_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL, BT_GAP_SCAN_FAST_INTERVAL
  );
  static struct bt_le_conn_param defaultConnParam = BT_LE_CONN_PARAM_INIT(
    //BT_GAP_INIT_CONN_INT_MIN, BT_GAP_INIT_CONN_INT_MAX, 0, 400
    //12, 12 // aka 15ms, default from apple documentation
    0x50, 0x50, // aka 80ms, from nRF SDK LLPM sample
    0, 400
  );
  // Note for apple see: https://developer.apple.com/library/archive/qa/qa1931/_index.html
  // And https://developer.apple.com/accessories/Accessory-Design-Guidelines.pdf (BLE section)

  static struct bt_le_scan_param defaultScanParam = //BT_LE_SCAN_PASSIVE;
  {
		.type       = BT_LE_SCAN_TYPE_PASSIVE, // passive scan
		.options    = BT_LE_SCAN_OPT_FILTER_DUPLICATE, // Scans for EVERYTHING
		.interval   = BT_GAP_SCAN_FAST_INTERVAL, // 0x0010, // V.FAST, NOT BT_GAP_SCAN_FAST_INTERVAL - gap.h
		.window     = BT_GAP_SCAN_FAST_WINDOW // 0x0010, // V.FAST, NOT BT_GAP_SCAN_FAST_INTERVAL - gap.h
	};

  /**
   * Why is this necessary? Traditional pointer-to-function cannot easily
   * and reliably be wrapped with std::function/bind/mem_fn. We also need
   * the Herald API to use subclasses for each platform, necessitating
   * some sort of static bridge. Not pretty, but works and allows us to
   * prevent nullptr problems
   */
  std::optional<std::shared_ptr<herald::zephyrinternal::Callbacks>> 
    concreteReceiverInstance;
  
  
  // static struct bt_conn* conn = NULL;
  
  // NOTE: The below is called multiple times for ONE char value. Keep appending to result until NULL==data.
  static uint8_t gatt_read_cb(struct bt_conn *conn, uint8_t err,
              struct bt_gatt_read_params *params,
              const void *data, uint16_t length)
  {
    if (concreteReceiverInstance.has_value()) {
      return concreteReceiverInstance.value()->gatt_read_cb(conn,err,params,data,length);
    }
    return length; // say we've consumed the data anyway
  }
  
  static struct bt_gatt_read_params read_params = {
    .func = gatt_read_cb,
    .handle_count = 1,
    .single = {
      .handle = 0x0000,
      .offset = 0x0000
    }
  };


  // void scan_init(void)
  // {
  //   // int err;

  //   // struct bt_scan_init_param scan_init = {
  //   //   .connect_if_match = 0, // no auto connect (handled by herald protocol coordinator)
  //   //   .scan_param = NULL,
  //   //   .conn_param = BT_LE_CONN_PARAM_DEFAULT
  //   // };

  //   // bt_scan_init(&scan_init);
  //   // bt_scan_cb_register(&scan_cb);

  //   /*
  //   err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, herald_uuid);
  //   if (err) {
  //     printk("Scanning filters cannot be set (err %d)\n", err);

  //     return;
  //   }

  //   err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
  //   if (err) {
  //     printk("Filters cannot be turned on (err %d)\n", err);
  //   }
  //   */
  // }

  
  static void connected(struct bt_conn *conn, uint8_t err)
  {
    if (concreteReceiverInstance.has_value()) {
      concreteReceiverInstance.value()->connected(conn,err);
    }
  }

  static void disconnected(struct bt_conn *conn, uint8_t reason)
  {
    if (concreteReceiverInstance.has_value()) {
      concreteReceiverInstance.value()->disconnected(conn,reason);
    }
  }

  static void le_param_updated(struct bt_conn *conn, uint16_t interval,
            uint16_t latency, uint16_t timeout)
  {
    if (concreteReceiverInstance.has_value()) {
      concreteReceiverInstance.value()->le_param_updated(conn,interval,latency,timeout);
    }
  }
  
	static struct bt_conn_cb conn_callbacks = {
		.connected = connected,
		.disconnected = disconnected,
		.le_param_updated = le_param_updated,
	};

  // static bt_addr_le_t *last_addr = BT_ADDR_LE_NONE;

  void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
  struct net_buf_simple *buf) {
    if (concreteReceiverInstance.has_value()) {
      concreteReceiverInstance.value()->scan_cb(addr,rssi,adv_type,buf);
    }
  }

  // BT_SCAN_CB_INIT(scan_cbs, scan_filter_match, );
  
	static struct bt_scan_init_param scan_init = {
		.scan_param = &defaultScanParam,
		.connect_if_match = false,
		.conn_param = &defaultConnParam
	};

  // void scan_filter_match(struct bt_scan_device_info *device_info,
  //             struct bt_scan_filter_match *filter_match,
  //             bool connectable)
  // {
  //   char addr[BT_ADDR_LE_STR_LEN];

  //   bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

  //   printk("Filters matched. Address: %s connectable: %s\n",
  //     addr, connectable ? "yes" : "no");
  // }

  // void scan_connecting_error(struct bt_scan_device_info *device_info)
  // {
  //   printk("Connecting failed\n");
  // }

  // void scan_connecting(struct bt_scan_device_info *device_info,
  //           struct bt_conn *conn)
  // {
  //   //default_conn = bt_conn_ref(conn);
  // }

  // void scan_filter_no_match(struct bt_scan_device_info *device_info,
  //         bool connectable)
  // {
  //   int err;
  //   struct bt_conn *conn;
  //   char addr[BT_ADDR_LE_STR_LEN];

  //   if (device_info->recv_info->adv_type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
  //     bt_addr_le_to_str(device_info->recv_info->addr, addr,
  //           sizeof(addr));
  //     printk("Direct advertising received from %s\n", addr);
  //     bt_scan_stop();

  //     err = bt_conn_le_create(device_info->recv_info->addr,
  //           BT_CONN_LE_CREATE_CONN,
  //           device_info->conn_param, &conn);

  //     if (!err) {
  //       default_conn = bt_conn_ref(conn);
  //       bt_conn_unref(conn);
  //     }
  //   }
  // }

  // BT_SCAN_CB_INIT(scan_cb, scan_filter_match, scan_filter_no_match,
  //     scan_connecting_error, scan_connecting);



  // GATT DISCOVERY INTERNAL METHODS
  
  static void discovery_completed_cb(struct bt_gatt_dm *dm,
            void *context)
  {
    if (concreteReceiverInstance.has_value()) {
      concreteReceiverInstance.value()->discovery_completed_cb(dm,context);
    }
  }

  static void discovery_service_not_found_cb(struct bt_conn *conn,
              void *context)
  {
    if (concreteReceiverInstance.has_value()) {
      concreteReceiverInstance.value()->discovery_service_not_found_cb(conn,context);
    }
  }

  static void discovery_error_found_cb(struct bt_conn *conn,
              int err,
              void *context)
  {
    if (concreteReceiverInstance.has_value()) {
      concreteReceiverInstance.value()->discovery_error_found_cb(conn,err,context);
    }
  }

  static const struct bt_gatt_dm_cb discovery_cb = {
    .completed = discovery_completed_cb,
    .service_not_found = discovery_service_not_found_cb,
    .error_found = discovery_error_found_cb,
  };

}


template <typename ContextT>
class ConcreteBLEReceiver<ContextT>::Impl : public herald::zephyrinternal::Callbacks {
public:
  Impl(ContextT& ctx, BluetoothStateManager& bluetoothStateManager, 
    std::shared_ptr<PayloadDataSupplier> payloadDataSupplier, 
    std::shared_ptr<BLEDatabase> bleDatabase);
  ~Impl();

  // Zephyr OS callbacks
  void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
      struct net_buf_simple *buf) override;

  void le_param_updated(struct bt_conn *conn, uint16_t interval,
            uint16_t latency, uint16_t timeout) override;
  void connected(struct bt_conn *conn, uint8_t err) override;
  void disconnected(struct bt_conn *conn, uint8_t reason) override;
  
  void discovery_completed_cb(struct bt_gatt_dm *dm, void *context) override;
  void discovery_service_not_found_cb(struct bt_conn *conn, void *context) override;
  void discovery_error_found_cb(struct bt_conn *conn, int err, void *context) override;

  uint8_t gatt_read_cb(struct bt_conn *conn, uint8_t err,
              struct bt_gatt_read_params *params,
              const void *data, uint16_t length) override;

  // std::optional<ConnectedDeviceState&> findState(const TargetIdentifier& forTarget);
  // std::optional<ConnectedDeviceState&> findStateByConnection(struct bt_conn *conn);
  ConnectedDeviceState& findOrCreateState(const TargetIdentifier& toTarget);
  ConnectedDeviceState& findOrCreateStateByConnection(struct bt_conn *conn, bool remoteInstigated = false);
  void removeState(const TargetIdentifier& forTarget);

  // internal call methods
  void startScanning();
  void stopScanning();
  void gatt_discover(struct bt_conn *conn);
      
  ContextT& m_context;
  BluetoothStateManager& m_stateManager;
  std::shared_ptr<PayloadDataSupplier> m_pds;
  std::shared_ptr<BLEDatabase> db;

  std::vector<std::shared_ptr<SensorDelegate>> delegates;

  std::map<TargetIdentifier,ConnectedDeviceState> connectionStates;
  bool isScanning;

  HLOGGER(ContextT);
};

template <typename ContextT>
ConcreteBLEReceiver<ContextT>::Impl::Impl(ContextT& ctx, BluetoothStateManager& bluetoothStateManager, 
  std::shared_ptr<PayloadDataSupplier> payloadDataSupplier, 
  std::shared_ptr<BLEDatabase> bleDatabase)
  : m_context(ctx), // Herald API guarantees this to be safe
    m_stateManager(bluetoothStateManager),
    m_pds(payloadDataSupplier),
    db(bleDatabase),
    delegates(),
    connectionStates(),
    isScanning(false)
    HLOGGERINIT(ctx,"Sensor","BLE.ConcreteBLEReceiver")
{
  ;
}

template <typename ContextT>
ConcreteBLEReceiver<ContextT>::Impl::~Impl()
{
  ;
}

// NOTE: Optional references currently illegal in C++17 (Would need Boost)

// std::optional<ConnectedDeviceState&>
// ConcreteBLEReceiver::Impl::findState(const TargetIdentifier& forTarget)
// {
//   auto iter = connectionStates.find(forTarget);
//   if (connectionStates.end() != iter) {
//     return iter->second;
//   }
//   return {};
// }

// std::optional<ConnectedDeviceState&>
// ConcreteBLEReceiver::Impl::findStateByConnection(struct bt_conn *conn)
// {
//   for (const auto& [key, value] : connectionStates) {
//     if (value.connection == conn) {
//       return value;
//     }
//   }
//   return {};
// }

template <typename ContextT>
ConnectedDeviceState&
ConcreteBLEReceiver<ContextT>::Impl::findOrCreateState(const TargetIdentifier& forTarget)
{
  auto iter = connectionStates.find(forTarget);
  if (connectionStates.end() != iter) {
    return iter->second;
  }
  return connectionStates.emplace(forTarget, forTarget).first->second;
  // return connectionStates.find(forTarget)->second;
}

template <typename ContextT>
ConnectedDeviceState&
ConcreteBLEReceiver<ContextT>::Impl::findOrCreateStateByConnection(struct bt_conn *conn, bool remoteInstigated)
{
  for (auto& [key, value] : connectionStates) {
    if (value.connection == conn) {
      return value;
    }
  }
  // Create target identifier from address
  auto addr = bt_conn_get_dst(conn);
  BLEMacAddress bleMacAddress(addr->a.val);
  TargetIdentifier target((Data)bleMacAddress);
  auto result = connectionStates.emplace(target, target);
  bt_addr_le_copy(&result.first->second.address,addr);
  result.first->second.remoteInstigated = remoteInstigated;
  return result.first->second;
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::removeState(const TargetIdentifier& forTarget)
{
  auto iter = connectionStates.find(forTarget);
  if (connectionStates.end() != iter) {
    connectionStates.erase(iter);
  }
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::stopScanning()
{
  if (isScanning) {
    isScanning = false;
    bt_le_scan_stop();
  }
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::startScanning()
{
  if (isScanning) {
    return;
  }
  int err = bt_le_scan_start(&zephyrinternal::defaultScanParam, &zephyrinternal::scan_cb); // scan_cb linked via BT_SCAN_CB_INIT call
  
  if (0 != err) {
		HTDBG("Starting scanning failed");
		return;
	}
  isScanning = true;
}
  
template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
  struct net_buf_simple *buf)
{  
  // identify device by both MAC and potential pseudoDeviceAddress
  BLEMacAddress bleMacAddress(addr->a.val);
  Data advert(buf->data,buf->len);
  auto device = db->device(bleMacAddress,advert);

  // auto device = db->device(target);
  if (device->ignore()) {
    // device->rssi(RSSI(rssi)); // TODO should we do this so our update date works and shows this as a 'live' device?
    return;
  }

  // // Now pass to relevant BLEDatabase API call
  if (!device->rssi().has_value()) {
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    std::string addrStr(addr_str);
    HTDBG("New address FROM SCAN:-");
    HTDBG(addr_str);
  }

  // Add this RSSI reading - called at the end to ensure all other data variables set
  device->rssi(RSSI(rssi));
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::gatt_discover(struct bt_conn *conn)
{
  HTDBG("Attempting GATT service discovery");
  int err;

  // begin introspection
  err = bt_gatt_dm_start(conn, &zephyrinternal::herald_uuid.uuid, &zephyrinternal::discovery_cb, NULL);
  if (err) {
    HTDBG("could not start the discovery procedure, error code")
    HTDBG(std::to_string(err));
    bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN); // ensures disconnect() called, and loop completed
    return;
  }
  HTDBG("Service discovery succeeded... now do something with it in the callback!");
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::le_param_updated(struct bt_conn *conn, uint16_t interval,
            uint16_t latency, uint16_t timeout)
{
  HTDBG("le param updated called");
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::connected(struct bt_conn *conn, uint8_t err)
{
  HTDBG("**************** Zephyr connection callback. Mac of connected:");

  auto addr = bt_conn_get_dst(conn);
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
  std::string addrStr(addr_str);
  BLEMacAddress bleMacAddress(addr->a.val);
  HTDBG((std::string)bleMacAddress);

  ConnectedDeviceState& state = findOrCreateStateByConnection(conn, true);
  auto device = db->device(bleMacAddress); // Find by actual current physical address

  if (err) { // 2 = SMP issues? StreetPass blocker on Android device perhaps. Disabled SMP use?
    // When connecting to some devices (E.g. HTC Vive base station), you will connect BUT get an error code
    // The below ensures that this is counted as a connection failure

    HTDBG("Connected: Error value:-");
    HTDBG(std::to_string(err));
    // Note: See Bluetooth Specification, Vol 2. Part D (Error codes)

		bt_conn_unref(conn);
    
    state.state = BLEDeviceState::disconnected;
    state.connection = NULL;
      
    // Log last disconnected time in BLE database
    device->state(BLEDeviceState::disconnected);

    // if (targetForConnection.has_value() && connCallback.has_value()) {
    //   connCallback.value()(targetForConnection.value(),false);
    // }
		return;
  }

  state.connection = conn;
  bt_addr_le_copy(&state.address,addr);
  state.state = BLEDeviceState::connected;

  // Log last connected time in BLE database
  device->state(BLEDeviceState::connected);

  
  // if (targetForConnection.has_value() && connCallback.has_value()) {
  //   connCallback.value()(targetForConnection.value(),true);
  // }

}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::disconnected(struct bt_conn *conn, uint8_t reason)
{
  HTDBG("********** Zephyr disconnection callback. Mac of disconnected:");

  auto addr = bt_conn_get_dst(conn);
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
  std::string addrStr(addr_str);
  BLEMacAddress bleMacAddress(addr->a.val);
  HTDBG((std::string)bleMacAddress);

  if (reason) {
    HTDBG("Disconnection: Reason value:-");
    HTDBG(std::to_string(reason));
    // Note: See Bluetooth Specification, Vol 2. Part D (Error codes)
    // 0x20 = Unsupported LL parameter value
  }
  
  // TODO log disconnection time in ble database
  
	bt_conn_unref(conn);
  ConnectedDeviceState& state = findOrCreateStateByConnection(conn);

  state.state = BLEDeviceState::disconnected;
  state.connection = NULL;

  // Log last disconnected time in BLE database
  auto device = db->device(bleMacAddress); // Find by actual current physical address
  device->state(BLEDeviceState::disconnected);
}

// Discovery callbacks

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::discovery_completed_cb(struct bt_gatt_dm *dm,
				   void *context)
{
	HTDBG("The GATT discovery procedure succeeded");
  const struct bt_gatt_dm_attr *prev = NULL;
  bool found = false;
  ConnectedDeviceState& state = findOrCreateStateByConnection(bt_gatt_dm_conn_get(dm));
  auto device = db->device(state.target);
  do {
    prev = bt_gatt_dm_char_next(dm,prev);
    if (NULL != prev) {
      // Check for match of uuid to a herald read payload char
      struct bt_gatt_chrc *chrc = bt_gatt_dm_attr_chrc_val(prev);

      int matches = bt_uuid_cmp(chrc->uuid, &zephyrinternal::herald_char_payload_uuid.uuid);
      if (0 == matches) {
        HTDBG("    - FOUND Herald read characteristic. Reading.");
        device->payloadCharacteristic(BLESensorConfiguration::payloadCharacteristicUUID);
        // initialise payload data for this state
        state.readPayload.clear();

        // if match, for a read
        found = true;
        // set handles

        // TODO REFACTOR THE ACTUAL FETCHING OF PAYLOAD TO READPAYLOAD FUNCTION
        //  - Actually important, as currently a wearable will request the char multiple times from iOS before a reply is received
        zephyrinternal::read_params.single.handle = chrc->value_handle;
        zephyrinternal::read_params.single.offset = 0x0000; // gets changed on each use
        int readErr = bt_gatt_read(bt_gatt_dm_conn_get(dm), &zephyrinternal::read_params);
        if (readErr) {
          HTDBG("GATT read error: TBD");//, readErr);
          // bt_conn_disconnect(bt_gatt_dm_conn_get(dm), BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        }

        continue; // check for other characteristics too
      }
      matches = bt_uuid_cmp(chrc->uuid, &zephyrinternal::herald_char_signal_android_uuid.uuid);
      if (0 == matches) {
        HTDBG("    - FOUND Herald android signal characteristic. logging.");
        device->signalCharacteristic(BLESensorConfiguration::androidSignalCharacteristicUUID);
        device->operatingSystem(BLEDeviceOperatingSystem::android);

        continue; // check for other characteristics too
      }
      matches = bt_uuid_cmp(chrc->uuid, &zephyrinternal::herald_char_signal_ios_uuid.uuid);
      if (0 == matches) {
        HTDBG("    - FOUND Herald ios signal characteristic. logging.");
        device->signalCharacteristic(BLESensorConfiguration::iosSignalCharacteristicUUID);
        device->operatingSystem(BLEDeviceOperatingSystem::ios);

        continue; // check for other characteristics too
      }
      // otherwise
      char uuid_str[32];
      bt_uuid_to_str(chrc->uuid,uuid_str,sizeof(uuid_str));
      HTDBG("    - Char doesn't match any herald char uuid:-"); //, log_strdup(uuid_str));
      HTDBG(uuid_str);
    }
  } while (NULL != prev);

  if (!found) {
    HTDBG("Herald read payload char not found in herald service (weird...). Ignoring device.");
    device->ignore(true);
    // bt_conn_disconnect(bt_gatt_dm_conn_get(dm), BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  }

  // No it doesn't - this is safe: does ending this here break our bt_gatt_read? (as it uses that connection?)
  int err = bt_gatt_dm_data_release(dm);
  if (err) {
    HTDBG("Could not release the discovery data, error code: TBD");
    // bt_conn_disconnect(bt_gatt_dm_conn_get(dm), BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  }

  // very last action - for concurrency reasons (C++17 threading/mutex/async/future not available on Zephyr)
  std::vector<UUID> serviceList;
  serviceList.push_back(BLESensorConfiguration::serviceUUID);
  device->services(serviceList);
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::discovery_service_not_found_cb(struct bt_conn *conn,
					   void *context)
{
	HTDBG("The service could not be found during the discovery. Ignoring device:");
  ConnectedDeviceState& state = findOrCreateStateByConnection(conn);
  HTDBG((std::string)state.target);

  auto device = db->device(state.target);
  std::vector<UUID> serviceList; // empty service list // TODO put other listened-for services here
  device->services(serviceList);
  device->ignore(true);
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::Impl::discovery_error_found_cb(struct bt_conn *conn,
				     int err,
				     void *context)
{
	HTDBG("The discovery procedure failed with ");
  HTDBG(std::to_string(err));
  // TODO decide if we should ignore the device here, or just keep trying
}

template <typename ContextT>
uint8_t
ConcreteBLEReceiver<ContextT>::Impl::gatt_read_cb(struct bt_conn *conn, uint8_t err,
              struct bt_gatt_read_params *params,
              const void *data, uint16_t length)
{
  // Fetch state for this element
  ConnectedDeviceState& state = findOrCreateStateByConnection(conn);
  if (NULL == data) {
    HTDBG("Finished reading CHAR read payload:-");
    HTDBG(state.readPayload.hexEncodedString());
    
    // Set final read payload (triggers success callback on observer)
    db->device(state.target)->payloadData(state.readPayload);

    return 0;
  }

  state.readPayload.append((const uint8_t*)data,0,length);
  return length;
}








template <typename ContextT>
ConcreteBLEReceiver<ContextT>::ConcreteBLEReceiver(ContextT& ctx, BluetoothStateManager& bluetoothStateManager, 
  std::shared_ptr<PayloadDataSupplier> payloadDataSupplier, std::shared_ptr<BLEDatabase> bleDatabase)
  : mImpl(std::make_shared<Impl>(ctx,bluetoothStateManager,payloadDataSupplier,bleDatabase))
{
  ;
}

template <typename ContextT>
ConcreteBLEReceiver<ContextT>::~ConcreteBLEReceiver()
{
  ;
}

template <typename ContextT>
std::optional<std::shared_ptr<CoordinationProvider>>
ConcreteBLEReceiver<ContextT>::coordinationProvider()
{
  return {}; // we don't provide this, ConcreteBLESensor provides this. We provide HeraldV1ProtocolProvider
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::add(const std::shared_ptr<SensorDelegate>& delegate)
{
  mImpl->delegates.push_back(delegate);
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::start()
{
  HDBG("ConcreteBLEReceiver::start");
  if (!BLESensorConfiguration::scanningEnabled) {
    HDBG("Sensor Configuration has scanning disabled. Returning.");
    return;
  }
  herald::ble::zephyrinternal::concreteReceiverInstance = mImpl;
  

  // Ensure our zephyr context has bluetooth ready
  HDBG("calling start bluetooth");
  int startOk = mImpl->m_context->startBluetooth();
  HDBG("start bluetooth done");
  if (0 != startOk) {
    HDBG("ERROR starting context bluetooth:-");
    HDBG(std::to_string(startOk));
  }

  HDBG("Calling conn cb register");
	bt_conn_cb_register(&zephyrinternal::conn_callbacks);
  HDBG("conn cb register done");

  HDBG("calling bt scan start");
  mImpl->startScanning();

  HDBG("ConcreteBLEReceiver::start completed successfully");
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::stop()
{
  HDBG("ConcreteBLEReceiver::stop");
  if (!BLESensorConfiguration::scanningEnabled) {
    HDBG("Sensor Configuration has scanning disabled. Returning.");
    return;
  }
  
  herald::ble::zephyrinternal::concreteReceiverInstance.reset(); // destroys the shared_ptr not necessarily the underlying value

  mImpl->stopScanning();

  // Don't stop Bluetooth altogether - this is done by the ZephyrContext->stopBluetooth() function only
  
  HDBG("ConcreteBLEReceiver::stop completed successfully");
}


template <typename ContextT>
bool
ConcreteBLEReceiver<ContextT>::immediateSend(Data data, const TargetIdentifier& targetIdentifier)
{
  return false; // TODO implement this
}

template <typename ContextT>
bool
ConcreteBLEReceiver<ContextT>::immediateSendAll(Data data)
{
  return false; // TODO implement this
}


// Herald V1 Protocol Provider overrides

// void
// ConcreteBLEReceiver::openConnection(const TargetIdentifier& toTarget, const HeraldConnectionCallback& connCallback)
// {
template <typename ContextT>
bool
ConcreteBLEReceiver<ContextT>::openConnection(const TargetIdentifier& toTarget)
{
  HDBG("openConnection");

  // Create addr from TargetIdentifier data
  ConnectedDeviceState& state = mImpl->findOrCreateState(toTarget);
  uint8_t val[6] = {0,0,0,0,0,0};
  Data addrData = (Data)toTarget; // TODO change this to mac for target ID
  uint8_t t;
  bool cok = addrData.uint8(0,t);
  if (cok)
  val[0] = t;
  cok = addrData.uint8(1,t);
  if (cok)
  val[1] = t;
  cok = addrData.uint8(2,t);
  if (cok)
  val[2] = t;
  cok = addrData.uint8(3,t);
  if (cok)
  val[3] = t;
  cok = addrData.uint8(4,t);
  if (cok)
  val[4] = t;
  cok = addrData.uint8(5,t);
  if (cok)
  val[5] = t;
  // TODO create a convenience function in Data for the above
  
  // TODO don't assume RANDOM (1) in the below
  bt_addr_le_t tempAddress{1, {{val[0],val[1],val[2],val[3],val[4],val[5]}}};
  // state.address = BT_ADDR_LE_NONE;
  bt_addr_le_copy(&state.address, &tempAddress);
  HDBG("Address copied. Constituted as:-");
  // idiot check of copied data
  Data newAddr(state.address.a.val,6);
  BLEMacAddress newMac(newAddr);
  HDBG((std::string)newMac);




  // // print out device info
  // BLEMacAddress mac(addrData);
  // std::string di("Opening Connection :: Device info: mac=");
  // di += (std::string)mac;
  // di += ", os=";
  // auto devPtr = mImpl->db->device(toTarget);
  // auto os = devPtr->operatingSystem();
  // if (os.has_value()) {
  //   if (herald::ble::BLEDeviceOperatingSystem::ios == os) {
  //     di += "ios";
  //   } else if (herald::ble::BLEDeviceOperatingSystem::android == os) {
  //     di += "android";
  //   }
  // } else {
  //   di += "unknown";
  // }
  // di += ", ignore=";
  // auto ignore = devPtr->ignore();
  // if (ignore) {
  //   di += "true";
  // } else {
  //   di += "false";
  // }

  // HDBG(di);
  
  
  // temporarily stop scan - WORKAROUND for https://github.com/zephyrproject-rtos/zephyr/issues/20660
  // HDBG("pausing scanning");
  mImpl->stopScanning();
  mImpl->m_context->getAdvertiser().stopAdvertising();
  // HDBG("Scanning paused");


  // attempt connection, if required
  bool ok = true;
  if (NULL == state.connection) {
    HDBG(" - No existing connection. Attempting to connect");
    // std::stringstream os;
    // os << " - Create Param: Interval: " << zephyrinternal::defaultCreateParam.interval
    //    << ", Window: " << zephyrinternal::defaultCreateParam.window
    //    << ", Timeout: " << zephyrinternal::defaultCreateParam.timeout
    //    << " | Conn Param: Interval Min: " << zephyrinternal::BTLEConnParam->interval_min
    //    << ", Interval Max: " << zephyrinternal::defaultConnParam.interval_max
    //    << ", latency: " << zephyrinternal::defaultConnParam.latency
    //    << ", timeout: " << zephyrinternal::defaultConnParam.timeout
    //    << std::ends
    //    ;
    // HDBG(os.str());
    // HDBG(std::to_string(zephyrinternal::defaultCreateParam.interval));
    // HDBG(std::to_string(zephyrinternal::defaultCreateParam.window));
    // HDBG(std::to_string(zephyrinternal::defaultCreateParam.timeout));
    // HDBG(std::to_string(zephyrinternal::defaultConnParam.interval_min));
    // HDBG(std::to_string(zephyrinternal::defaultConnParam.interval_max));
    // HDBG(std::to_string(zephyrinternal::defaultConnParam.latency));
    // HDBG(std::to_string(zephyrinternal::defaultConnParam.timeout));
    // HDBG("Random address check ok?");
    // HDBG(bt_le_scan_random_addr_check() ? "yes" : "no");
    
  	char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(&state.address, addr_str, sizeof(addr_str));
    HDBG("ADDR AS STRING in openConnection:-");
    HDBG(addr_str);

    state.state = BLEDeviceState::connecting; // this is used by the condition variable
    state.remoteInstigated = false; // as we're now definitely the instigators
    int success = bt_conn_le_create(
      &state.address,
      &zephyrinternal::defaultCreateParam,
      &zephyrinternal::defaultConnParam,
      &state.connection
    );
    HDBG(" - post connection attempt");
    if (0 != success) {
      ok = false;
      if (-EINVAL == success) {
        HDBG(" - ERROR in passed in parameters");
      } else if (-EAGAIN == success) {
        HDBG(" - bt device not ready");
      } else if (-EALREADY == success) {
        HDBG(" - bt device initiating")
      } else if (-ENOMEM == success) {
        HDBG(" - bt connect attempt failed with default BT ID. Trying again later.");
        // auto device = mImpl->db->device(toTarget);
        // device->ignore(true);
      } else if (-ENOBUFS == success) {
        HDBG(" - bt_hci_cmd_create has no buffers free");
      } else if (-ECONNREFUSED == success) {
        HDBG(" - Connection refused");
      } else if (-EIO == success) {
        HDBG(" - Low level BT HCI opcode IO failure");
      } else {
        HDBG(" - Unknown error code...");
        HDBG(std::to_string(success));
      }

      // Add to ignore list for now
      // DONT DO THIS HERE - MANY REASONS IT CAN FAIL auto device = mImpl->db->device(toTarget);
      // HDBG(" - Ignoring following target: {}", toTarget);
      // device->ignore(true);
      
      // Log last disconnected time in BLE database (records failure, allows progressive backoff)
      auto device = mImpl->db->device(newMac); // Find by actual current physical address
      device->state(BLEDeviceState::disconnected);
      
      // Immediately restart advertising on failure, but not scanning
      mImpl->m_context->getAdvertiser().startAdvertising();

      return false;
    } else {
      HDBG("Zephyr waitWithTimeout for new connection");
      // lock and wait for connection to be created
      
      // STD::ASYNC/MUTEX variant:-
      // std::unique_lock<std::mutex> lk(mImpl->bleInUse);
      // mImpl->connectionAvailable.wait(lk, [this] {
      //   return mImpl->connectionState == BLEDeviceState::connecting;
      // }); // BLOCKS
      // verify connection successful
      // connCallback(toTarget,mImpl->connectionState == BLEDeviceState::connected);

      // ZEPHYR SPECIFIC VARIANT
      uint32_t timedOut = waitWithTimeout(5'000, K_MSEC(25), [&state] {
        return state.state == BLEDeviceState::connecting;
      });
      if (timedOut != 0) {
        HDBG("ZEPHYR WAIT TIMED OUT. Is connected?");
        HDBG((state.state == BLEDeviceState::connected) ? "true" : "false");
        HDBG(std::to_string(timedOut));
        return false;
      }
      // return mImpl->connectionState == BLEDeviceState::connected;
      return state.state == BLEDeviceState::connected;
    }
  } else {
    HDBG(" - Existing connection exists! Reusing.");
    return true;
  }
}

// void
// ConcreteBLEReceiver::closeConnection(const TargetIdentifier& toTarget, const HeraldConnectionCallback& connCallback)
// {
//   connCallback(toTarget,false);
// }

// void
// ConcreteBLEReceiver::serviceDiscovery(Activity activity, CompletionCallback callback)
// {
//   callback(activity,{});
// }

// void
// ConcreteBLEReceiver::readPayload(Activity activity, CompletionCallback callback)
// {
//   callback(activity,{});
// }

// void
// ConcreteBLEReceiver::immediateSend(Activity activity, CompletionCallback callback)
// {
//   callback(activity,{});
// }

// void
// ConcreteBLEReceiver::immediateSendAll(Activity activity, CompletionCallback callback)
// {
//   callback(activity,{});
// }

template <typename ContextT>
bool
ConcreteBLEReceiver<ContextT>::closeConnection(const TargetIdentifier& toTarget)
{
  HDBG("closeConnection call for ADDR:-");
  ConnectedDeviceState& state = mImpl->findOrCreateState(toTarget);
  char addr_str[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(&state.address, addr_str, sizeof(addr_str));
  HDBG(addr_str);
  if (NULL != state.connection) {
    if (state.remoteInstigated) {
      HDBG("Connection remote instigated - not forcing close");
    } else {
      bt_conn_disconnect(state.connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      // auto device = mImpl->db.device(toTarget);
      // device->registerDisconnect(Date());
    }
  } else {
    // Can clear the remote instigated flag as they've closed the connection
    state.remoteInstigated = false;
  }
  if (!state.remoteInstigated) {
    mImpl->removeState(toTarget);
    return false; // assumes we've closed it // TODO proper multi-connection state tracking
  }
  return true; // remote instigated the connection - keep it open and inform caller
}

template <typename ContextT>
void
ConcreteBLEReceiver<ContextT>::restartScanningAndAdvertising()
{
  // Print out current list of devices and their info
  if (!mImpl->connectionStates.empty()) {
    HDBG("Current connection states cached:-");
    for (auto& [key,value] : mImpl->connectionStates) {
      std::string ci = " - ";
      ci += ((Data)value.target).hexEncodedString();
      ci += " state: ";
      switch (value.state) {
        case BLEDeviceState::connected:
          ci += "connected";
          break;
        case BLEDeviceState::disconnected:
          ci += "disconnected";
          break;
        default:
          ci += "connecting";
      }
      ci += " connection is null: ";
      ci += (NULL == value.connection ? "true" : "false");
      HDBG(ci);

      // Check connection reference is valid by address - has happened with non connectable devices (VR headset bluetooth stations)
      value.connection = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &value.address);
      // If the above returns null, the next iterator will remove our state

      // Check for non null connection but disconnected state
      
      if (BLEDeviceState::disconnected == value.state) {
        value.connection = NULL;
      }
      // Now check for timeout - nRF Connect doesn't cause a disconnect callback
      if (NULL != value.connection && value.remoteInstigated) {
        HDBG("REMOTELY INSTIGATED OR CONNECTED DEVICE TIMED OUT");
        auto device = mImpl->db->device(value.target);
        if (device->timeIntervalSinceConnected() < TimeInterval::never() &&
            device->timeIntervalSinceConnected() > TimeInterval::seconds(30)) {
          // disconnect
          bt_conn_disconnect(value.connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
          value.connection = NULL;
        }
      }
    }

    // Do internal clean up too - remove states no longer required
    for (auto iter = mImpl->connectionStates.begin();mImpl->connectionStates.end() != iter; ++iter) {
      if (NULL == iter->second.connection) { // means Zephyr callbacks are finished with the connection object (i.e. disconnect was called)
        mImpl->connectionStates.erase(iter);
      }
    }
  }

  // Restart scanning
  // HDBG("restartScanningAndAdvertising - requesting scanning and advertising restarts");
  mImpl->startScanning();
  mImpl->m_context->getAdvertiser().startAdvertising();
}

template <typename ContextT>
std::optional<Activity>
ConcreteBLEReceiver<ContextT>::serviceDiscovery(Activity activity)
{
  auto currentTargetOpt = std::get<1>(activity.prerequisites.front());
  if (!currentTargetOpt.has_value()) {
    HDBG("No target specified for serviceDiscovery activity. Returning.");
    return {}; // We've been asked to connect to no specific target - not valid for Bluetooth
  }
  // Ensure we have a cached state (i.e. we are connected)
  auto& state = mImpl->findOrCreateState(currentTargetOpt.value());
  if (state.state != BLEDeviceState::connected) {
    HDBG("Not connected to target of activity. Returning.");
    return {};
  }
  if (NULL == state.connection) {
    HDBG("State for activity does not have a connection. Returning.");
    return {};
  }
  auto device = mImpl->db->device(currentTargetOpt.value());

  mImpl->gatt_discover(state.connection);

  uint32_t timedOut = waitWithTimeout(5'000, K_MSEC(25), [&device] () -> bool {
    return !device->hasServicesSet(); // service discovery not completed yet
  });

  if (0 != timedOut) {
    HDBG("service discovery timed out for device");
    HDBG(std::to_string(timedOut));
    return {};
  }
  return {};
}

template <typename ContextT>
std::optional<Activity>
ConcreteBLEReceiver<ContextT>::readPayload(Activity activity)
{
  return {};
}

template <typename ContextT>
std::optional<Activity>
ConcreteBLEReceiver<ContextT>::immediateSend(Activity activity)
{
  return {};
}

template <typename ContextT>
std::optional<Activity>
ConcreteBLEReceiver<ContextT>::immediateSendAll(Activity activity)
{
  return {};
}

}
}
