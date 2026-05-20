/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <AppMain.h>
#include <cstdint>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>

#include <app-common/zap-generated/callback.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/ConcreteAttributePath.h>
#include <app/EventLogging.h>
#include <app/reporting/reporting.h>
#include <app/util/af-types.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <app/util/util.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/ZclString.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <pthread.h>
#include <sys/ioctl.h>

#include "CommissionableInit.h"
#include "Device.h"
#include "DeviceDimmable.h"
#include "main.h"
#include "wemo_bridge/wemo_adapter_openwemo.h"
#include <app/server/Server.h>

#include <app/clusters/identify-server/IdentifyCluster.h>
#include <app/clusters/identify-server/identify-server.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <app/DefaultTimerDelegate.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace chip;
using namespace chip::app;
using namespace chip::Credentials;
using namespace chip::Inet;
using namespace chip::Transport;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;

// These variables need to be in global scope for bridged-actions-stub.cpp to access them
std::vector<Room *> gRooms;
std::vector<Action *> gActions;

namespace {

NamedPipeCommands sChipNamedPipeCommands;
BridgeCommandDelegate sBridgeCommandDelegate;

const int kNodeLabelSize = 32;
const int kUniqueIdSize  = 32;
// Current ZCL implementation of Struct uses a max-size array of 254 bytes
const int kDescriptorAttributeArraySize = 254;

EndpointId gCurrentEndpointId;
EndpointId gFirstDynamicEndpointId;
// Power source is on the same endpoint as the composed device
Device * gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT + 1];

const int16_t minMeasuredValue     = -27315;
const int16_t maxMeasuredValue     = 32766;
const int16_t initialMeasuredValue = 100;

// ENDPOINT DEFINITIONS:
// =================================================================================
//
// Endpoint definitions will be reused across multiple endpoints for every instance of the
// endpoint type.
// There will be no intrinsic storage for the endpoint attributes declared here.
// Instead, all attributes will be treated as EXTERNAL, and therefore all reads
// or writes to the attributes must be handled within the emberAfExternalAttributeWriteCallback
// and emberAfExternalAttributeReadCallback functions declared herein. This fits
// the typical model of a bridge, since a bridge typically maintains its own
// state database representing the devices connected to it.

// Device types for dynamic endpoints: TODO Need a generated file from ZAP to define these!
// (taken from matter-devices.xml)
#define DEVICE_TYPE_BRIDGED_NODE 0x0013
// (taken from lo-devices.xml)
#define DEVICE_TYPE_LO_ON_OFF_LIGHT 0x0100
// (taken from lo-devices.xml)
#define DEVICE_TYPE_LO_DIMMABLE_LIGHT 0x0101
// (taken from matter-devices.xml)
#define DEVICE_TYPE_POWER_SOURCE 0x0011
// (taken from matter-devices.xml)
#define DEVICE_TYPE_TEMP_SENSOR 0x0302

// Device Version for dynamic endpoints:
#define DEVICE_VERSION_DEFAULT 1

// ---------------------------------------------------------------------------
//
// LIGHT ENDPOINT: contains the following clusters:
//   - On/Off
//   - Descriptor
//   - Bridged Device Basic Information

// Declare On/Off cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, 0), /* on/off */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Descriptor cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* device list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* server list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* client list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),  /* parts list */
#if CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::EndpointUniqueID::Id, ARRAY, 32, 0), /* endpoint unique id*/
#endif
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Bridged Device Basic Information cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, kNodeLabelSize,
                          ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(EXTERNAL_STORAGE)),         /* NodeLabel */
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0), /* Reachable */
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::UniqueID::Id, CHAR_STRING, kUniqueIdSize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::ConfigurationVersion::Id, INT32U, 4,
                              0), /* Configuration Version */
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::FeatureMap::Id, BITMAP32, 4, 0), /* feature map */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Cluster List for Bridged Light endpoint
// TODO: It's not clear whether it would be better to get the command lists from
// the ZAP config on our last fixed endpoint instead.
constexpr CommandId onOffIncomingCommands[] = {
    app::Clusters::OnOff::Commands::Off::Id,
    app::Clusters::OnOff::Commands::On::Id,
    app::Clusters::OnOff::Commands::Toggle::Id,
    app::Clusters::OnOff::Commands::OffWithEffect::Id,
    app::Clusters::OnOff::Commands::OnWithRecallGlobalScene::Id,
    app::Clusters::OnOff::Commands::OnWithTimedOff::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

// Declare Bridged Light endpoint
DECLARE_DYNAMIC_ENDPOINT(bridgedLightEndpoint, bridgedLightClusters);
DataVersion gLight1DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];
DataVersion gLight2DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];

// ---------------------------------------------------------------------------
//
// DIMMABLE LIGHT ENDPOINT: contains the following clusters:
//   - On/Off
//   - Level Control
//   - Descriptor
//   - Bridged Device Basic Information

// Declare LevelControl cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(levelControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::CurrentLevel::Id, INT8U, 1, 0),  /* CurrentLevel (nullable) */
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MinLevel::Id, INT8U, 1, 0),  /* MinLevel */
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MaxLevel::Id, INT8U, 1, 0),  /* MaxLevel */
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::ClusterRevision::Id, INT16U, 2, 0), /* ClusterRevision */
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0),    /* FeatureMap */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId levelControlIncomingCommands[] = {
    LevelControl::Commands::MoveToLevel::Id,
    LevelControl::Commands::Move::Id,
    LevelControl::Commands::Step::Id,
    LevelControl::Commands::Stop::Id,
    LevelControl::Commands::MoveToLevelWithOnOff::Id,
    LevelControl::Commands::MoveWithOnOff::Id,
    LevelControl::Commands::StepWithOnOff::Id,
    LevelControl::Commands::StopWithOnOff::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedDimmableLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(LevelControl::Id, levelControlAttrs, ZAP_CLUSTER_MASK(SERVER), levelControlIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(bridgedDimmableLightEndpoint, bridgedDimmableLightClusters);

DeviceOnOff Light1("Light 1", "Office");
DeviceOnOff Light2("Light 2", "Office");

DeviceTempSensor TempSensor1("TempSensor 1", "Office", minMeasuredValue, maxMeasuredValue, initialMeasuredValue);
DeviceTempSensor TempSensor2("TempSensor 2", "Office", minMeasuredValue, maxMeasuredValue, initialMeasuredValue);

// Declare Bridged endpoints used for Action clusters
DataVersion gActionLight1DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];
DataVersion gActionLight2DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];
DataVersion gActionLight3DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];
DataVersion gActionLight4DataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];

DeviceOnOff ActionLight1("Action Light 1", "Room 1");
DeviceOnOff ActionLight2("Action Light 2", "Room 1");
DeviceOnOff ActionLight3("Action Light 3", "Room 2");
DeviceOnOff ActionLight4("Action Light 4", "Room 2");

// WeMo bridge adapter talking to wemo_ctrl/openwemo engine.
wemo_bridge::WemoAdapterOpenWemo gWemoAdapter("127.0.0.1:49153");

// Max cluster count across both endpoint types for DataVersion storage.
constexpr size_t kMaxBridgedClusters = MATTER_ARRAY_SIZE(bridgedDimmableLightClusters);

struct BridgedWemoLight
{
    int wemo_id = 0;
    std::string udn;
    bool is_dimmable = false;
    std::unique_ptr<Device> device;  // DeviceOnOff or DeviceDimmable
    std::array<DataVersion, kMaxBridgedClusters> dataVersions {};

    // --- Command-echo suppression ---
    // Keep commanded values for a short settle window so stale post-command
    // events from wemo_ctrl cannot flip state back.
    int commandedOnOff = -1;  // -1 = no pending command, 0 = OFF, 1 = ON
    int commandedLevel = -1;  // -1 = no pending command, 0-254 = level
    std::chrono::steady_clock::time_point commandedOnOffUntil;
    std::chrono::steady_clock::time_point commandedLevelUntil;
};

constexpr auto kCommandSettleWindow = std::chrono::milliseconds(2000);

std::vector<BridgedWemoLight> gBridgedWemoLights;
std::unordered_map<Device *, std::string> gWemoDeviceToUdn;

// Setup composed device with two temperature sensors and a power source
ComposedDevice gComposedDevice("Composed Device", "Bedroom");
DeviceTempSensor ComposedTempSensor1("Composed TempSensor 1", "Bedroom", minMeasuredValue, maxMeasuredValue, initialMeasuredValue);
DeviceTempSensor ComposedTempSensor2("Composed TempSensor 2", "Bedroom", minMeasuredValue, maxMeasuredValue, initialMeasuredValue);
DevicePowerSource ComposedPowerSource("Composed Power Source", "Bedroom", PowerSource::Feature::kBattery);

Room room1("Room 1", 0xE001, Actions::EndpointListTypeEnum::kRoom, true);
Room room2("Room 2", 0xE002, Actions::EndpointListTypeEnum::kRoom, true);
Room room3("Zone 3", 0xE003, Actions::EndpointListTypeEnum::kZone, false);

Action action1(0x1001, "Room 1 On", Actions::ActionTypeEnum::kAutomation, 0xE001, 0x1, Actions::ActionStateEnum::kInactive, true);
Action action2(0x1002, "Turn On Room 2", Actions::ActionTypeEnum::kAutomation, 0xE002, 0x01, Actions::ActionStateEnum::kInactive,
               true);
Action action3(0x1003, "Turn Off Room 1", Actions::ActionTypeEnum::kAutomation, 0xE003, 0x01, Actions::ActionStateEnum::kInactive,
               false);

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(tempSensorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(TemperatureMeasurement::Attributes::MeasuredValue::Id, INT16S, 2, 0),        /* Measured Value */
    DECLARE_DYNAMIC_ATTRIBUTE(TemperatureMeasurement::Attributes::MinMeasuredValue::Id, INT16S, 2, 0), /* Min Measured Value */
    DECLARE_DYNAMIC_ATTRIBUTE(TemperatureMeasurement::Attributes::MaxMeasuredValue::Id, INT16S, 2, 0), /* Max Measured Value */
    DECLARE_DYNAMIC_ATTRIBUTE(TemperatureMeasurement::Attributes::FeatureMap::Id, BITMAP32, 4, 0),     /* FeatureMap */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// ---------------------------------------------------------------------------
//
// TEMPERATURE SENSOR ENDPOINT: contains the following clusters:
//   - Temperature measurement
//   - Descriptor
//   - Bridged Device Basic Information
DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedTempSensorClusters)
DECLARE_DYNAMIC_CLUSTER(TemperatureMeasurement::Id, tempSensorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

// Declare Bridged Light endpoint
DECLARE_DYNAMIC_ENDPOINT(bridgedTempSensorEndpoint, bridgedTempSensorClusters);
DataVersion gTempSensor1DataVersions[MATTER_ARRAY_SIZE(bridgedTempSensorClusters)];
DataVersion gTempSensor2DataVersions[MATTER_ARRAY_SIZE(bridgedTempSensorClusters)];

// ---------------------------------------------------------------------------
//
// COMPOSED DEVICE ENDPOINT: contains the following clusters:
//   - Descriptor
//   - Bridged Device Basic Information
//   - Power source

// Composed Device Configuration
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(powerSourceAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(PowerSource::Attributes::BatChargeLevel::Id, ENUM8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(PowerSource::Attributes::BatReplacementNeeded::Id, BOOLEAN, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(PowerSource::Attributes::BatReplaceability::Id, ENUM8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(PowerSource::Attributes::Order::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(PowerSource::Attributes::Status::Id, ENUM8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(PowerSource::Attributes::Description::Id, CHAR_STRING, 32, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(PowerSource::Attributes::EndpointList::Id, ARRAY, 0, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(PowerSource::Attributes::FeatureMap::Id, BITMAP32, 4, 0), DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedComposedDeviceClusters)
DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(PowerSource::Id, powerSourceAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(bridgedComposedDeviceEndpoint, bridgedComposedDeviceClusters);
DataVersion gComposedDeviceDataVersions[MATTER_ARRAY_SIZE(bridgedComposedDeviceClusters)];
DataVersion gComposedTempSensor1DataVersions[MATTER_ARRAY_SIZE(bridgedTempSensorClusters)];
DataVersion gComposedTempSensor2DataVersions[MATTER_ARRAY_SIZE(bridgedTempSensorClusters)];

// Identify cluster on the aggregator endpoint (endpoint 1).  The ZAP config
// declares Identify with EXTERNAL_STORAGE attributes, so we need an actual
// IdentifyCluster instance registered with the codegen data model provider.
DefaultTimerDelegate sIdentifyTimerDelegate;
RegisteredServerCluster<Clusters::IdentifyCluster>
    gIdentifyClusterEp1(Clusters::IdentifyCluster::Config(1, sIdentifyTimerDelegate)
                            .WithIdentifyType(Clusters::Identify::IdentifyTypeEnum::kNone));

} // namespace

// REVISION DEFINITIONS:
// =================================================================================

#define ZCL_DESCRIPTOR_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_CLUSTER_REVISION (2u)
#define ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_FEATURE_MAP (0u)
#define ZCL_FIXED_LABEL_CLUSTER_REVISION (1u)
#define ZCL_ON_OFF_CLUSTER_REVISION (4u)
#define ZCL_TEMPERATURE_SENSOR_CLUSTER_REVISION (1u)
#define ZCL_TEMPERATURE_SENSOR_FEATURE_MAP (0u)
#define ZCL_POWER_SOURCE_CLUSTER_REVISION (2u)
#define ZCL_LEVEL_CONTROL_CLUSTER_REVISION (6u)
#define ZCL_LEVEL_CONTROL_FEATURE_MAP (0x03u) // OnOff + Lighting feature bits

// ---------------------------------------------------------------------------

int AddDeviceEndpoint(Device * dev, EmberAfEndpointType * ep, const Span<const EmberAfDeviceType> & deviceTypeList,
                      const Span<DataVersion> & dataVersionStorage,
#if CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
                      chip::CharSpan epUniqueId,
#endif
                      chip::EndpointId parentEndpointId = chip::kInvalidEndpointId)
{
    uint8_t index = 0;
    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (nullptr == gDevices[index])
        {
            gDevices[index] = dev;
            CHIP_ERROR err;
            while (true)
            {
                // Todo: Update this to schedule the work rather than use this lock
                DeviceLayer::StackLock lock;
                dev->SetEndpointId(gCurrentEndpointId);
                dev->SetParentEndpointId(parentEndpointId);
#if !CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
                err =
                    emberAfSetDynamicEndpoint(index, gCurrentEndpointId, ep, dataVersionStorage, deviceTypeList, parentEndpointId);
#else
                err = emberAfSetDynamicEndpointWithEpUniqueId(index, gCurrentEndpointId, ep, dataVersionStorage, deviceTypeList,
                                                              epUniqueId, parentEndpointId);
#endif
                if (err == CHIP_NO_ERROR)
                {
                    ChipLogProgress(DeviceLayer, "Added device %s to dynamic endpoint %d (index=%d)", dev->GetName(),
                                    gCurrentEndpointId, index);

                    if (dev->GetUniqueId()[0] == '\0')
                    {
                        dev->GenerateUniqueId();
                    }

                    return index;
                }
                if (err != CHIP_ERROR_ENDPOINT_EXISTS)
                {
                    gDevices[index] = nullptr;
                    return -1;
                }
                // Handle wrap condition
                if (++gCurrentEndpointId < gFirstDynamicEndpointId)
                {
                    gCurrentEndpointId = gFirstDynamicEndpointId;
                }
            }
        }
        index++;
    }
    ChipLogProgress(DeviceLayer, "Failed to add dynamic endpoint: No endpoints available!");
    return -1;
}

int RemoveDeviceEndpoint(Device * dev)
{
    uint8_t index = 0;
    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (gDevices[index] == dev)
        {
            // Todo: Update this to schedule the work rather than use this lock
            DeviceLayer::StackLock lock;
            // Silence complaints about unused ep when progress logging
            // disabled.
            [[maybe_unused]] EndpointId ep = emberAfClearDynamicEndpoint(index);
            gDevices[index]                = nullptr;
            ChipLogProgress(DeviceLayer, "Removed device %s from dynamic endpoint %d (index=%d)", dev->GetName(), ep, index);
            return index;
        }
        index++;
    }
    return -1;
}

std::vector<EndpointListInfo> GetEndpointListInfo(chip::EndpointId parentId)
{
    // This bridge currently does not expose the Actions cluster model to controllers.
    // Returning empty lists avoids touching dynamic endpoint internals that are not
    // needed for WeMo OnOff bridging.
    (void) parentId;
    return {};
}

std::vector<Action *> GetActionListInfo(chip::EndpointId parentId)
{
    (void) parentId;
    return {};
}

std::vector<Room *> GetRoomListInfo(chip::EndpointId parentId)
{
    (void) parentId;
    return {};
}

namespace {
void CallReportingCallback(intptr_t closure)
{
    auto path = reinterpret_cast<app::ConcreteAttributePath *>(closure);
    MatterReportingAttributeChangeCallback(*path);
    Platform::Delete(path);
}

void ScheduleReportingCallback(Device * dev, ClusterId cluster, AttributeId attribute)
{
    auto * path = Platform::New<app::ConcreteAttributePath>(dev->GetEndpointId(), cluster, attribute);
    TEMPORARY_RETURN_IGNORED PlatformMgr().ScheduleWork(CallReportingCallback, reinterpret_cast<intptr_t>(path));
}
} // anonymous namespace

void HandleDeviceStatusChanged(Device * dev, Device::Changed_t itemChangedMask)
{
    if (itemChangedMask & Device::kChanged_Reachable)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::Reachable::Id);
    }

    if (itemChangedMask & Device::kChanged_Name)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
    }
}

void HandleDeviceOnOffStatusChanged(DeviceOnOff * dev, DeviceOnOff::Changed_t itemChangedMask)
{
    if (itemChangedMask & (DeviceOnOff::kChanged_Reachable | DeviceOnOff::kChanged_Name | DeviceOnOff::kChanged_Location))
    {
        HandleDeviceStatusChanged(static_cast<Device *>(dev), (Device::Changed_t) itemChangedMask);
    }

    if (itemChangedMask & DeviceOnOff::kChanged_OnOff)
    {
        ScheduleReportingCallback(dev, OnOff::Id, OnOff::Attributes::OnOff::Id);
    }
}

void HandleDeviceDimmableStatusChanged(DeviceDimmable * dev, DeviceDimmable::Changed_t itemChangedMask)
{
    if (itemChangedMask & (DeviceDimmable::kChanged_Reachable | DeviceDimmable::kChanged_Name))
    {
        HandleDeviceStatusChanged(static_cast<Device *>(dev), (Device::Changed_t) itemChangedMask);
    }

    if (itemChangedMask & DeviceOnOff::kChanged_OnOff)
    {
        ScheduleReportingCallback(dev, OnOff::Id, OnOff::Attributes::OnOff::Id);
    }

    if (itemChangedMask & DeviceDimmable::kChanged_Level)
    {
        ScheduleReportingCallback(dev, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    }
}

void HandleDevicePowerSourceStatusChanged(DevicePowerSource * dev, DevicePowerSource::Changed_t itemChangedMask)
{
    using namespace app::Clusters;
    if (itemChangedMask &
        (DevicePowerSource::kChanged_Reachable | DevicePowerSource::kChanged_Name | DevicePowerSource::kChanged_Location))
    {
        HandleDeviceStatusChanged(static_cast<Device *>(dev), (Device::Changed_t) itemChangedMask);
    }

    if (itemChangedMask & DevicePowerSource::kChanged_BatLevel)
    {
        MatterReportingAttributeChangeCallback(dev->GetEndpointId(), PowerSource::Id, PowerSource::Attributes::BatChargeLevel::Id);
    }

    if (itemChangedMask & DevicePowerSource::kChanged_Description)
    {
        MatterReportingAttributeChangeCallback(dev->GetEndpointId(), PowerSource::Id, PowerSource::Attributes::Description::Id);
    }
    if (itemChangedMask & DevicePowerSource::kChanged_EndpointList)
    {
        MatterReportingAttributeChangeCallback(dev->GetEndpointId(), PowerSource::Id, PowerSource::Attributes::EndpointList::Id);
    }
}

void HandleDeviceTempSensorStatusChanged(DeviceTempSensor * dev, DeviceTempSensor::Changed_t itemChangedMask)
{
    if (itemChangedMask &
        (DeviceTempSensor::kChanged_Reachable | DeviceTempSensor::kChanged_Name | DeviceTempSensor::kChanged_Location))
    {
        HandleDeviceStatusChanged(static_cast<Device *>(dev), (Device::Changed_t) itemChangedMask);
    }
    if (itemChangedMask & DeviceTempSensor::kChanged_MeasurementValue)
    {
        ScheduleReportingCallback(dev, TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id);
    }
}

Protocols::InteractionModel::Status HandleReadBridgedDeviceBasicAttribute(Device * dev, chip::AttributeId attributeId,
                                                                          uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace BridgedDeviceBasicInformation::Attributes;

    ChipLogProgress(DeviceLayer, "HandleReadBridgedDeviceBasicAttribute: attrId=%d, maxReadLength=%d", attributeId, maxReadLength);

    if ((attributeId == Reachable::Id) && (maxReadLength == 1))
    {
        *buffer = dev->IsReachable() ? 1 : 0;
    }
    else if ((attributeId == NodeLabel::Id) && (maxReadLength == 32))
    {
        MutableByteSpan zclNameSpan(buffer, maxReadLength);
        TEMPORARY_RETURN_IGNORED MakeZclCharString(zclNameSpan, dev->GetName());
    }
    else if ((attributeId == UniqueID::Id) && (maxReadLength == 32))
    {
        MutableByteSpan zclUniqueIdSpan(buffer, maxReadLength);
        TEMPORARY_RETURN_IGNORED MakeZclCharString(zclUniqueIdSpan, dev->GetUniqueId());
    }
    else if ((attributeId == ConfigurationVersion::Id) && (maxReadLength == 4))
    {
        uint32_t configVersion = dev->GetConfigurationVersion();
        memcpy(buffer, &configVersion, sizeof(configVersion));
    }
    else if ((attributeId == ClusterRevision::Id) && (maxReadLength == 2))
    {
        uint16_t rev = ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_CLUSTER_REVISION;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else if ((attributeId == FeatureMap::Id) && (maxReadLength == 4))
    {
        uint32_t featureMap = ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_FEATURE_MAP;
        memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadOnOffAttribute(DeviceOnOff * dev, chip::AttributeId attributeId, uint8_t * buffer,
                                                             uint16_t maxReadLength)
{
    ChipLogProgress(DeviceLayer, "HandleReadOnOffAttribute: attrId=%d, maxReadLength=%d", attributeId, maxReadLength);

    if ((attributeId == OnOff::Attributes::OnOff::Id) && (maxReadLength == 1))
    {
        *buffer = dev->IsOn() ? 1 : 0;
    }
    else if ((attributeId == OnOff::Attributes::ClusterRevision::Id) && (maxReadLength == 2))
    {
        uint16_t rev = ZCL_ON_OFF_CLUSTER_REVISION;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteOnOffAttribute(DeviceOnOff * dev, chip::AttributeId attributeId, uint8_t * buffer)
{
    ChipLogProgress(DeviceLayer, "HandleWriteOnOffAttribute: attrId=%d", attributeId);

    if ((attributeId == OnOff::Attributes::OnOff::Id) && (dev->IsReachable()))
    {
        const bool targetOn = (*buffer != 0);

        // Update internal state and respond to the controller immediately.
        // The WeMo IPC is fired asynchronously so it does not block the
        // Matter event loop (SQLite lookup + TCP roundtrip to wemo_ctrl).
        dev->SetOnOff(targetOn);

        auto udnIt = gWemoDeviceToUdn.find(static_cast<Device *>(dev));
        if (udnIt != gWemoDeviceToUdn.end())
        {
            const std::string udn = udnIt->second;
            // Record commanded state so echo events are suppressed until confirmed.
            for (auto & entry : gBridgedWemoLights)
            {
                if (entry.device.get() == static_cast<Device *>(dev))
                {
                    entry.commandedOnOff = targetOn ? 1 : 0;
                    entry.commandedOnOffUntil = std::chrono::steady_clock::now() + kCommandSettleWindow;
                    break;
                }
            }
            std::thread([udn, targetOn]() { gWemoAdapter.SetOnOff(udn, targetOn); }).detach();
        }
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadLevelControlAttribute(DeviceDimmable * dev, chip::AttributeId attributeId,
                                                                     uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace LevelControl::Attributes;

    ChipLogProgress(DeviceLayer, "HandleReadLevelControlAttribute: attrId=0x%04x, maxReadLength=%d", attributeId, maxReadLength);

    if ((attributeId == CurrentLevel::Id) && (maxReadLength == 1))
    {
        *buffer = dev->GetLevel();
    }
    else if ((attributeId == MinLevel::Id) && (maxReadLength == 1))
    {
        *buffer = 1;
    }
    else if ((attributeId == MaxLevel::Id) && (maxReadLength == 1))
    {
        *buffer = 254;
    }
    else if ((attributeId == ClusterRevision::Id) && (maxReadLength == 2))
    {
        uint16_t rev = ZCL_LEVEL_CONTROL_CLUSTER_REVISION;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else if ((attributeId == FeatureMap::Id) && (maxReadLength == 4))
    {
        uint32_t featureMap = ZCL_LEVEL_CONTROL_FEATURE_MAP;
        memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteLevelControlAttribute(DeviceDimmable * dev, chip::AttributeId attributeId,
                                                                      uint8_t * buffer)
{
    ChipLogProgress(DeviceLayer, "HandleWriteLevelControlAttribute: attrId=0x%04x", attributeId);

    if ((attributeId == LevelControl::Attributes::CurrentLevel::Id) && (dev->IsReachable()))
    {
        const auto now = std::chrono::steady_clock::now();
        const uint8_t matterLevel = *buffer;
        BridgedWemoLight * matched = nullptr;

        for (auto & entry : gBridgedWemoLights)
        {
            if (entry.device.get() == static_cast<Device *>(dev))
            {
                matched = &entry;
                break;
            }
        }

        // Some controllers emit LevelControl writes as part of an OnOff toggle.
        // Preserve the current brightness in that window; level should only
        // change when user explicitly changes brightness.
        if (matched != nullptr && matched->commandedOnOff >= 0 && now > matched->commandedOnOffUntil)
        {
            matched->commandedOnOff = -1;
        }

        if (matched != nullptr && matched->commandedOnOff >= 0 && now <= matched->commandedOnOffUntil)
        {
            ChipLogProgress(DeviceLayer, "Ignoring transient level write during OnOff settle for %s", dev->GetName());
            return Protocols::InteractionModel::Status::Success;
        }

        // Google Home "Off" can generate an internal MoveToLevel(1) before
        // OnOff=0. Ignore that synthetic min-level write so brightness is
        // preserved across Off/On toggles.
        if (matched != nullptr && matched->commandedOnOff < 0 && matterLevel <= 1)
        {
            ChipLogProgress(DeviceLayer, "Ignoring synthetic min-level write for %s", dev->GetName());
            return Protocols::InteractionModel::Status::Success;
        }

        dev->SetLevel(matterLevel);

        // Convert Matter 0-254 -> WeMo 0-100 and dispatch asynchronously.
        uint8_t wemoPercent = static_cast<uint8_t>(static_cast<uint16_t>(matterLevel) * 100u / 254u);
        // Preserve "on" semantics for tiny non-zero Matter levels (e.g. 1),
        // which would otherwise truncate to 0% and turn the device off.
        if (matterLevel > 0 && wemoPercent == 0)
        {
            wemoPercent = 1;
        }
        auto udnIt = gWemoDeviceToUdn.find(static_cast<Device *>(dev));
        if (udnIt != gWemoDeviceToUdn.end())
        {
            const std::string udn = udnIt->second;
            // Record commanded level so echo events are suppressed until confirmed.
            for (auto & entry : gBridgedWemoLights)
            {
                if (entry.device.get() == static_cast<Device *>(dev))
                {
                    entry.commandedLevel = matterLevel;
                    entry.commandedLevelUntil = std::chrono::steady_clock::now() + kCommandSettleWindow;
                    break;
                }
            }
            std::thread([udn, wemoPercent]() { gWemoAdapter.SetLevelPercent(udn, wemoPercent); }).detach();
        }
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteBridgedDeviceBasicAttribute(Device * dev, AttributeId attributeId, uint8_t * buffer)
{
    ChipLogProgress(DeviceLayer, "HandleWriteBridgedDeviceBasicAttribute: attrId=" ChipLogFormatMEI, ChipLogValueMEI(attributeId));

    if (attributeId != BridgedDeviceBasicInformation::Attributes::NodeLabel::Id)
    {
        return Protocols::InteractionModel::Status::UnsupportedWrite;
    }

    CharSpan nameSpan = CharSpan::fromZclString(buffer);

    if (nameSpan.size() > kNodeLabelSize)
    {
        return Protocols::InteractionModel::Status::ConstraintError;
    }

    std::string name(nameSpan.data(), nameSpan.size());
    dev->SetName(name.c_str());

    HandleDeviceStatusChanged(dev, Device::kChanged_Name);

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadTempMeasurementAttribute(DeviceTempSensor * dev, chip::AttributeId attributeId,
                                                                       uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace TemperatureMeasurement::Attributes;

    if ((attributeId == MeasuredValue::Id) && (maxReadLength == 2))
    {
        int16_t measuredValue = dev->GetMeasuredValue();
        memcpy(buffer, &measuredValue, sizeof(measuredValue));
    }
    else if ((attributeId == MinMeasuredValue::Id) && (maxReadLength == 2))
    {
        int16_t minValue = dev->mMin;
        memcpy(buffer, &minValue, sizeof(minValue));
    }
    else if ((attributeId == MaxMeasuredValue::Id) && (maxReadLength == 2))
    {
        int16_t maxValue = dev->mMax;
        memcpy(buffer, &maxValue, sizeof(maxValue));
    }
    else if ((attributeId == FeatureMap::Id) && (maxReadLength == 4))
    {
        uint32_t featureMap = ZCL_TEMPERATURE_SENSOR_FEATURE_MAP;
        memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    else if ((attributeId == ClusterRevision::Id) && (maxReadLength == 2))
    {
        uint16_t clusterRevision = ZCL_TEMPERATURE_SENSOR_CLUSTER_REVISION;
        memcpy(buffer, &clusterRevision, sizeof(clusterRevision));
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                                         const EmberAfAttributeMetadata * attributeMetadata,
                                                                         uint8_t * buffer, uint16_t maxReadLength)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;

    if ((endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) && (gDevices[endpointIndex] != nullptr))
    {
        Device * dev = gDevices[endpointIndex];

        if (clusterId == BridgedDeviceBasicInformation::Id)
        {
            ret = HandleReadBridgedDeviceBasicAttribute(dev, attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == OnOff::Id)
        {
            ret = HandleReadOnOffAttribute(static_cast<DeviceOnOff *>(dev), attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == LevelControl::Id)
        {
            ret = HandleReadLevelControlAttribute(static_cast<DeviceDimmable *>(dev), attributeMetadata->attributeId, buffer,
                                                   maxReadLength);
        }
        else if (clusterId == TemperatureMeasurement::Id)
        {
            ret = HandleReadTempMeasurementAttribute(static_cast<DeviceTempSensor *>(dev), attributeMetadata->attributeId, buffer,
                                                     maxReadLength);
        }
    }

    return ret;
}

class BridgedPowerSourceAttrAccess : public AttributeAccessInterface
{
public:
    // Register on all endpoints.
    BridgedPowerSourceAttrAccess() : AttributeAccessInterface(Optional<EndpointId>::Missing(), PowerSource::Id) {}

    CHIP_ERROR
    Read(const ConcreteReadAttributePath & aPath, AttributeValueEncoder & aEncoder) override
    {
        uint16_t powerSourceDeviceIndex = CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT;

        if ((gDevices[powerSourceDeviceIndex] != nullptr))
        {
            DevicePowerSource * dev = static_cast<DevicePowerSource *>(gDevices[powerSourceDeviceIndex]);
            if (aPath.mEndpointId != dev->GetEndpointId())
            {
                return CHIP_IM_GLOBAL_STATUS(UnsupportedEndpoint);
            }
            switch (aPath.mAttributeId)
            {
            case PowerSource::Attributes::BatChargeLevel::Id:
                TEMPORARY_RETURN_IGNORED aEncoder.Encode(dev->GetBatChargeLevel());
                break;
            case PowerSource::Attributes::Order::Id:
                TEMPORARY_RETURN_IGNORED aEncoder.Encode(dev->GetOrder());
                break;
            case PowerSource::Attributes::Status::Id:
                TEMPORARY_RETURN_IGNORED aEncoder.Encode(dev->GetStatus());
                break;
            case PowerSource::Attributes::Description::Id:
                TEMPORARY_RETURN_IGNORED aEncoder.Encode(
                    chip::CharSpan(dev->GetDescription().c_str(), dev->GetDescription().size()));
                break;
            case PowerSource::Attributes::EndpointList::Id: {
                std::vector<chip::EndpointId> & list = dev->GetEndpointList();
                DataModel::List<EndpointId> dm_list(chip::Span<chip::EndpointId>(list.data(), list.size()));
                TEMPORARY_RETURN_IGNORED aEncoder.Encode(dm_list);
                break;
            }
            case PowerSource::Attributes::ClusterRevision::Id:
                TEMPORARY_RETURN_IGNORED aEncoder.Encode(ZCL_POWER_SOURCE_CLUSTER_REVISION);
                break;
            case PowerSource::Attributes::FeatureMap::Id:
                TEMPORARY_RETURN_IGNORED aEncoder.Encode(dev->GetFeatureMap());
                break;

            case PowerSource::Attributes::BatReplacementNeeded::Id:
                TEMPORARY_RETURN_IGNORED aEncoder.Encode(false);
                break;
            case PowerSource::Attributes::BatReplaceability::Id:
                TEMPORARY_RETURN_IGNORED aEncoder.Encode(PowerSource::BatReplaceabilityEnum::kNotReplaceable);
                break;
            default:
                return CHIP_IM_GLOBAL_STATUS(UnsupportedAttribute);
            }
        }
        return CHIP_NO_ERROR;
    }
};

BridgedPowerSourceAttrAccess gPowerAttrAccess;

Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                                          const EmberAfAttributeMetadata * attributeMetadata,
                                                                          uint8_t * buffer)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;

    // ChipLogProgress(DeviceLayer, "emberAfExternalAttributeWriteCallback: ep=%d", endpoint);

    if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        Device * dev = gDevices[endpointIndex];

        if ((dev->IsReachable()) && (clusterId == OnOff::Id))
        {
            ret = HandleWriteOnOffAttribute(static_cast<DeviceOnOff *>(dev), attributeMetadata->attributeId, buffer);
        }
        else if ((dev->IsReachable()) && (clusterId == LevelControl::Id))
        {
            ret = HandleWriteLevelControlAttribute(static_cast<DeviceDimmable *>(dev), attributeMetadata->attributeId, buffer);
        }
        else if ((dev->IsReachable()) && (clusterId == BridgedDeviceBasicInformation::Id))
        {
            ret = HandleWriteBridgedDeviceBasicAttribute(dev, attributeMetadata->attributeId, buffer);
        }
    }

    return ret;
}

void runOnOffRoomAction(Room * room, bool actionOn, EndpointId endpointId, uint16_t actionID, uint32_t invokeID, bool hasInvokeID)
{
    if (hasInvokeID)
    {
        Actions::Events::StateChanged::Type event{ actionID, invokeID, Actions::ActionStateEnum::kActive };
        EventNumber eventNumber;
        TEMPORARY_RETURN_IGNORED chip::app::LogEvent(event, endpointId, eventNumber);
    }

    // Check and run the action for ActionLight1 - ActionLight4
    if (room->getName().compare(ActionLight1.GetLocation()) == 0)
    {
        ActionLight1.SetOnOff(actionOn);
    }
    if (room->getName().compare(ActionLight2.GetLocation()) == 0)
    {
        ActionLight2.SetOnOff(actionOn);
    }
    if (room->getName().compare(ActionLight3.GetLocation()) == 0)
    {
        ActionLight3.SetOnOff(actionOn);
    }
    if (room->getName().compare(ActionLight4.GetLocation()) == 0)
    {
        ActionLight4.SetOnOff(actionOn);
    }

    if (hasInvokeID)
    {
        Actions::Events::StateChanged::Type event{ actionID, invokeID, Actions::ActionStateEnum::kInactive };
        EventNumber eventNumber;
        TEMPORARY_RETURN_IGNORED chip::app::LogEvent(event, endpointId, eventNumber);
    }
}

const EmberAfDeviceType gBridgedOnOffDeviceTypes[] = { { DEVICE_TYPE_LO_ON_OFF_LIGHT, DEVICE_VERSION_DEFAULT },
                                                       { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };

const EmberAfDeviceType gBridgedDimmableDeviceTypes[] = { { DEVICE_TYPE_LO_DIMMABLE_LIGHT, DEVICE_VERSION_DEFAULT },
                                                           { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };

const EmberAfDeviceType gBridgedComposedDeviceTypes[] = { { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT },
                                                          { DEVICE_TYPE_POWER_SOURCE, DEVICE_VERSION_DEFAULT } };

const EmberAfDeviceType gComposedTempSensorDeviceTypes[] = { { DEVICE_TYPE_TEMP_SENSOR, DEVICE_VERSION_DEFAULT } };

const EmberAfDeviceType gBridgedTempSensorDeviceTypes[] = { { DEVICE_TYPE_TEMP_SENSOR, DEVICE_VERSION_DEFAULT },
                                                            { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };

#define POLL_INTERVAL_MS (100)
uint8_t poll_prescale = 0;

bool kbhit()
{
    int byteswaiting;
    ioctl(0, FIONREAD, &byteswaiting);
    return byteswaiting > 0;
}

const int16_t oneDegree = 100;

void * bridge_polling_thread(void * context)
{
    bool light1_added = true;
    bool light2_added = false;
    while (true)
    {
        if (kbhit())
        {
            int ch = getchar();

            // Commands used for the actions bridge test plan.
            if (ch == '2' && light2_added == false)
            {
                // TC-BR-2 step 2, Add Light2
#if !CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
                AddDeviceEndpoint(&Light2, &bridgedLightEndpoint, Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                                  Span<DataVersion>(gLight2DataVersions), 1);
#else
                AddDeviceEndpoint(&Light2, &bridgedLightEndpoint, Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                                  Span<DataVersion>(gLight2DataVersions), ""_span, 1);
#endif
                light2_added = true;
            }
            else if (ch == '4' && light1_added == true)
            {
                // TC-BR-2 step 4, Remove Light 1
                RemoveDeviceEndpoint(&Light1);
                light1_added = false;
            }
            if (ch == '5' && light1_added == false)
            {
                // TC-BR-2 step 5, Add Light 1 back
#if !CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
                AddDeviceEndpoint(&Light2, &bridgedLightEndpoint, Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                                  Span<DataVersion>(gLight2DataVersions), 1);
#else
                AddDeviceEndpoint(&Light1, &bridgedLightEndpoint, Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                                  Span<DataVersion>(gLight1DataVersions), ""_span, 1);
#endif
                light1_added = true;
            }
            if (ch == 'b')
            {
                // TC-BR-3 step 1b, rename lights
                if (light1_added)
                {
                    Light1.SetName("Light 1b");
                }
                if (light2_added)
                {
                    Light2.SetName("Light 2b");
                }
            }
            if (ch == 'c')
            {
                // TC-BR-3 step 2c, change the state of the lights
                if (light1_added)
                {
                    Light1.Toggle();
                }
                if (light2_added)
                {
                    Light2.Toggle();
                }
            }
            if (ch == 't')
            {
                // TC-BR-4 step 1g, change the state of the temperature sensors
                TempSensor1.SetMeasuredValue(static_cast<int16_t>(TempSensor1.GetMeasuredValue() + oneDegree));
                TempSensor2.SetMeasuredValue(static_cast<int16_t>(TempSensor2.GetMeasuredValue() + oneDegree));
                ComposedTempSensor1.SetMeasuredValue(static_cast<int16_t>(ComposedTempSensor1.GetMeasuredValue() + oneDegree));
                ComposedTempSensor2.SetMeasuredValue(static_cast<int16_t>(ComposedTempSensor2.GetMeasuredValue() + oneDegree));
            }

            // Commands used for the actions cluster test plan.
            if (ch == 'r')
            {
                // TC-ACT-2.2 step 2c, rename "Room 1"
                room1.setName("Room 1 renamed");
                ActionLight1.SetLocation(room1.getName());
                ActionLight2.SetLocation(room1.getName());
            }
            if (ch == 'f')
            {
                // TC-ACT-2.2 step 2f, move "Action Light 3" from "Room 2" to "Room 1"
                ActionLight3.SetLocation(room1.getName());
            }
            if (ch == 'i')
            {
                // TC-ACT-2.2 step 2i, remove "Room 2" (make it not visible in the endpoint list), do not remove the lights
                room2.setIsVisible(false);
            }
            if (ch == 'l')
            {
                // TC-ACT-2.2 step 2l, add a new "Zone 3" and add "Action Light 2" to the new zone
                room3.setIsVisible(true);
                ActionLight2.SetZone("Zone 3");
            }
            if (ch == 'm')
            {
                // TC-ACT-2.2 step 3c, rename "Turn on Room 1 lights"
                action1.setName("Turn On Room 1");
            }
            if (ch == 'n')
            {
                // TC-ACT-2.2 step 3f, remove "Turn on Room 2 lights"
                action2.setIsVisible(false);
            }
            if (ch == 'o')
            {
                // TC-ACT-2.2 step 3i, add "Turn off Room 1 renamed lights"
                action3.setIsVisible(true);
            }

            // Commands used for the Bridged Device Basic Information test plan
            if (ch == 'u')
            {
                // TC-BRBINFO-2.2 step 2 "Set reachable to false"
                TempSensor1.SetReachable(false);
            }
            if (ch == 'v')
            {
                // TC-BRBINFO-2.2 step 2 "Set reachable to true"
                TempSensor1.SetReachable(true);
            }
            if (ch == 'w')
            {
                // TC-BRBINFO-3.2 step 3
                uint32_t configVersion = Light1.GetConfigurationVersion() + 1;
                Light1.SetConfigurationVersion(configVersion);
            }
            continue;
        }

        // Sleep to avoid tight loop reading commands
        usleep(POLL_INTERVAL_MS * 1000);
    }

    return nullptr;
}

namespace {

struct WemoEventContext
{
    int wemo_id;
    bool is_online;
    int state;
    int level; // 0-100 or -1
};

void HandleWemoEventOnMatterThread(intptr_t closure)
{
    auto * ctx = reinterpret_cast<WemoEventContext *>(closure);
    const auto now = std::chrono::steady_clock::now();
    for (auto & entry : gBridgedWemoLights)
    {
        if (entry.wemo_id == ctx->wemo_id)
        {
            auto * dev = entry.device.get();

            // Reachability always updates immediately.
            if (dev->IsReachable() != ctx->is_online)
            {
                dev->SetReachable(ctx->is_online);
            }

            if (ctx->is_online)
            {
                auto * light = static_cast<DeviceOnOff *>(dev);
                const bool newOn = (ctx->state != 0);
                const int newOnInt = newOn ? 1 : 0;
                bool suppressOnOff = false;

                // OnOff: suppress contradictory state events while the command
                // settle window is active.
                if (entry.commandedOnOff >= 0)
                {
                    if (now > entry.commandedOnOffUntil)
                    {
                        entry.commandedOnOff = -1;
                    }
                    else if (newOnInt != entry.commandedOnOff)
                    {
                        ChipLogProgress(DeviceLayer, "Suppressing echo for %s (got %s, commanded %s)",
                                        dev->GetName(), newOn ? "ON" : "OFF",
                                        entry.commandedOnOff ? "ON" : "OFF");
                        suppressOnOff = true;
                    }
                }

                if (!suppressOnOff && light->IsOn() != newOn)
                {
                    light->SetOnOff(newOn);
                }

                if (entry.is_dimmable && ctx->level >= 0)
                {
                    auto * dimmer = static_cast<DeviceDimmable *>(dev);
                    uint8_t matterLevel = static_cast<uint8_t>(static_cast<uint16_t>(ctx->level) * 254u / 100u);
                    bool suppressLevel = false;

                    if (entry.commandedLevel >= 0)
                    {
                        if (now > entry.commandedLevelUntil)
                        {
                            entry.commandedLevel = -1;
                        }
                        else if (matterLevel != static_cast<uint8_t>(entry.commandedLevel))
                        {
                            ChipLogProgress(DeviceLayer, "Suppressing level echo for %s (got %u, commanded %d)",
                                            dev->GetName(), matterLevel, entry.commandedLevel);
                            suppressLevel = true;
                        }
                    }

                    if (!suppressLevel && dimmer->GetLevel() != matterLevel)
                    {
                        dimmer->SetLevel(matterLevel);
                    }
                }
            }
            break;
        }
    }
    Platform::Delete(ctx);
}

} // namespace

void ApplicationInit()
{
    auto discovered = gWemoAdapter.Discover();
    std::sort(discovered.begin(), discovered.end(), [](const auto & a, const auto & b) {
        if (a.udn != b.udn)
        {
            return a.udn < b.udn;
        }
        return a.friendly_name < b.friendly_name;
    });

    // Clear out the device database
    memset(gDevices, 0, sizeof(gDevices));
    gBridgedWemoLights.clear();
    gWemoDeviceToUdn.clear();

    // Keep symbols referenced even when mock/action/temp endpoints are not published.
    (void) gLight1DataVersions;
    (void) gLight2DataVersions;
    (void) gActionLight1DataVersions;
    (void) gActionLight2DataVersions;
    (void) gActionLight3DataVersions;
    (void) gActionLight4DataVersions;
    (void) bridgedTempSensorEndpoint;
    (void) gTempSensor1DataVersions;
    (void) gTempSensor2DataVersions;
    (void) bridgedComposedDeviceEndpoint;
    (void) gComposedDeviceDataVersions;
    (void) gComposedTempSensor1DataVersions;
    (void) gComposedTempSensor2DataVersions;

    // Set starting endpoint id where dynamic endpoints will be assigned, which
    // will be the next consecutive endpoint id after the last fixed endpoint.
    gFirstDynamicEndpointId = static_cast<chip::EndpointId>(
        static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);
    gCurrentEndpointId = gFirstDynamicEndpointId;

    // Disable last fixed endpoint, which is used as a placeholder for all of the
    // supported clusters so that ZAP will generated the requisite code.
    emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);

    // Publish discovered WeMo lights up to dynamic endpoint capacity.
    const size_t endpointCapacity = CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT;
    gBridgedWemoLights.reserve(std::min(discovered.size(), endpointCapacity));

    for (const auto & dev : discovered)
    {
        if (gBridgedWemoLights.size() >= endpointCapacity)
        {
            ChipLogError(DeviceLayer, "Skipping WeMo device %s: dynamic endpoint capacity reached (%zu)", dev.friendly_name.c_str(),
                         endpointCapacity);
            continue;
        }

        BridgedWemoLight bridged;
        bridged.wemo_id = dev.wemo_id;
        bridged.udn = dev.udn;
        bridged.is_dimmable = dev.supports_level;
        const std::string name = dev.friendly_name.empty() ? std::string("WeMo Device") : dev.friendly_name;

        EmberAfEndpointType * epType;
        const EmberAfDeviceType * deviceTypes;
        size_t deviceTypesCount;

        if (dev.supports_level)
        {
            auto dimmer = std::make_unique<DeviceDimmable>(name.c_str(), "WeMo");
            dimmer->SetOnOff(dev.onoff != 0);
            // Seed level: WeMo 0-100 -> Matter 0-254
            dimmer->SetLevel(static_cast<uint8_t>(static_cast<uint16_t>(dev.level_percent) * 254u / 100u));
            dimmer->SetReachable(dev.is_online);
            bridged.device = std::move(dimmer);
            epType = &bridgedDimmableLightEndpoint;
            deviceTypes = gBridgedDimmableDeviceTypes;
            deviceTypesCount = MATTER_ARRAY_SIZE(gBridgedDimmableDeviceTypes);
            ChipLogProgress(DeviceLayer, "WeMo bind (dimmable): %s <- %s", name.c_str(), bridged.udn.c_str());
        }
        else
        {
            auto light = std::make_unique<DeviceOnOff>(name.c_str(), "WeMo");
            light->SetOnOff(dev.onoff != 0);
            light->SetReachable(dev.is_online);
            bridged.device = std::move(light);
            epType = &bridgedLightEndpoint;
            deviceTypes = gBridgedOnOffDeviceTypes;
            deviceTypesCount = MATTER_ARRAY_SIZE(gBridgedOnOffDeviceTypes);
            ChipLogProgress(DeviceLayer, "WeMo bind (on/off): %s <- %s", name.c_str(), bridged.udn.c_str());
        }

        // Push into the vector BEFORE registering the endpoint.  The CHIP SDK
        // stores the DataVersion span pointer for the lifetime of the endpoint,
        // so it must point into the vector's heap storage (which was pre-reserved
        // above) rather than into the stack-local `bridged` variable.
        gBridgedWemoLights.push_back(std::move(bridged));
        auto & stable = gBridgedWemoLights.back();

        // DataVersion span size must match the cluster count for the endpoint type.
        const size_t clusterCount = stable.is_dimmable
            ? MATTER_ARRAY_SIZE(bridgedDimmableLightClusters)
            : MATTER_ARRAY_SIZE(bridgedLightClusters);

#if !CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
        const int addedIndex = AddDeviceEndpoint(stable.device.get(), epType,
                                                 Span<const EmberAfDeviceType>(deviceTypes, deviceTypesCount),
                                                 Span<DataVersion>(stable.dataVersions.data(), clusterCount), 1);
#else
        // Use the WeMo UDN as the endpoint unique ID.  Strip the "uuid:"
        // prefix to fit within the 32-byte buffer.  This gives each bridged
        // device a stable identity that survives restarts.
        std::string epUniqueId = stable.udn;
        if (epUniqueId.rfind("uuid:", 0) == 0)
        {
            epUniqueId = epUniqueId.substr(5);
        }
        if (epUniqueId.size() > 32)
        {
            epUniqueId.resize(32);
        }
        CharSpan udnSpan(epUniqueId.c_str(), epUniqueId.size());
        const int addedIndex = AddDeviceEndpoint(stable.device.get(), epType,
                                                 Span<const EmberAfDeviceType>(deviceTypes, deviceTypesCount),
                                                 Span<DataVersion>(stable.dataVersions.data(), clusterCount), udnSpan, 1);
#endif
        if (addedIndex < 0)
        {
            ChipLogError(DeviceLayer, "Failed to publish WeMo device %s (udn=%s)", name.c_str(), stable.udn.c_str());
            gBridgedWemoLights.pop_back();
            continue;
        }

        if (stable.is_dimmable)
        {
            // Dynamic endpoints don't get cluster init functions called
            // automatically (DECLARE_DYNAMIC_CLUSTER passes NULL for the
            // functions array).  Manually init the LevelControl server so
            // its per-endpoint state (minLevel, maxLevel) is set up.
            emberAfLevelControlClusterServerInitCallback(stable.device->GetEndpointId());

            static_cast<DeviceDimmable *>(stable.device.get())->SetChangeCallback(&HandleDeviceDimmableStatusChanged);
        }
        else
        {
            static_cast<DeviceOnOff *>(stable.device.get())->SetChangeCallback(&HandleDeviceOnOffStatusChanged);
        }
        gWemoDeviceToUdn[stable.device.get()] = stable.udn;
    }

    // Receive state events from wemo_ctrl (called from wemo_engine IPC thread).
    // Dispatch to the Matter event loop to update bridged device state.
    gWemoAdapter.RegisterStateCallback([](const wemo_bridge::WemoStateEvent & ev) {
        auto * ctx       = Platform::New<WemoEventContext>();
        ctx->wemo_id     = ev.wemo_id;
        ctx->is_online   = ev.is_online;
        ctx->state       = ev.state;
        ctx->level       = ev.level;
        TEMPORARY_RETURN_IGNORED PlatformMgr().ScheduleWork(HandleWemoEventOnMatterThread, reinterpret_cast<intptr_t>(ctx));
    });

    // Re-trigger SSDP discovery now that the callback is registered.  Events
    // fired during the initial Discover() call were lost (callback not yet set).
    // This refresh causes wemo_ctrl to re-probe all devices and deliver fresh
    // state events, so bridged devices come online quickly after startup.
    gWemoAdapter.Refresh();

    gRooms.clear();
    gActions.clear();

    std::string path = std::string(LinuxDeviceOptions::GetInstance().app_pipe);

    if ((!path.empty()) and (sChipNamedPipeCommands.Start(path, &sBridgeCommandDelegate) != CHIP_NO_ERROR))
    {
        ChipLogError(NotSpecified, "Failed to start CHIP NamedPipeCommands");
        TEMPORARY_RETURN_IGNORED sChipNamedPipeCommands.Stop();
    }

    AttributeAccessInterfaceRegistry::Instance().Register(&gPowerAttrAccess);

    // Register the Identify cluster on the aggregator endpoint so attribute
    // reads are served by the IdentifyCluster implementation instead of falling
    // through to the external-attribute callback (which does not handle it).
    VerifyOrDie(CodegenDataModelProvider::Instance().Registry().Register(gIdentifyClusterEp1.Registration()) == CHIP_NO_ERROR);
}

void ApplicationShutdown() {}

int main(int argc, char * argv[])
{
    if (sChipNamedPipeCommands.Stop() != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "Failed to stop CHIP NamedPipeCommands");
    }

    if (ChipLinuxAppInit(argc, argv) != 0)
    {
        return -1;
    }
    ChipLinuxAppMainLoop();
    return 0;
}

BridgeAppCommandHandler * BridgeAppCommandHandler::FromJSON(const char * json)
{
    Json::Reader reader;
    Json::Value value;

    if (!reader.parse(json, value))
    {
        ChipLogError(NotSpecified, "Bridge App: Error parsing JSON with error %s:", reader.getFormattedErrorMessages().c_str());
        return nullptr;
    }

    if (value.empty() || !value.isObject())
    {
        ChipLogError(NotSpecified, "Bridge App: Invalid JSON command received");
        return nullptr;
    }

    if (!value.isMember("Name") || !value["Name"].isString())
    {
        ChipLogError(NotSpecified, "Bridge App: Invalid JSON command received: command name is missing");
        return nullptr;
    }

    return Platform::New<BridgeAppCommandHandler>(std::move(value));
}

void BridgeAppCommandHandler::HandleCommand(intptr_t context)
{
    auto * self      = reinterpret_cast<BridgeAppCommandHandler *>(context);
    std::string name = self->mJsonValue["Name"].asString();

    VerifyOrExit(!self->mJsonValue.empty(), ChipLogError(NotSpecified, "Invalid JSON event command received"));

    if (name == "SimulateConfigurationVersionChange")
    {
        uint32_t configVersion = Light1.GetConfigurationVersion() + 1;
        Light1.SetConfigurationVersion(configVersion);
    }
    else
    {
        ChipLogError(NotSpecified, "Unhandled command '%s': this should never happen", name.c_str());
        VerifyOrDie(false && "Named pipe command not supported, see log above.");
    }

exit:
    Platform::Delete(self);
}

void BridgeCommandDelegate::OnEventCommandReceived(const char * json)
{
    auto handler = BridgeAppCommandHandler::FromJSON(json);
    if (nullptr == handler)
    {
        ChipLogError(NotSpecified, "Bridge App: Unable to instantiate a command handler");
        return;
    }

    TEMPORARY_RETURN_IGNORED chip::DeviceLayer::PlatformMgr().ScheduleWork(BridgeAppCommandHandler::HandleCommand,
                                                                           reinterpret_cast<intptr_t>(handler));
}
