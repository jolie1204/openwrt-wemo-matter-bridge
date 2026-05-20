#pragma once

// Bridge endpoint capacity used by the Linux bridge-app model.
#define CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT 16
#define CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID 0

// Advertise this node as an Aggregator so commissioners/controllers classify
// it as a bridge root rather than an unknown device type.
#define CHIP_DEVICE_CONFIG_DEVICE_TYPE 0x000E

// Pull the baseline standalone platform config.
#include <CHIPProjectConfig.h>
