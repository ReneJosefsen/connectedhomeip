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

/**
 * @file DeviceCallbacks.cpp
 *
 * Implements all the callbacks to the application from the CHIP Stack
 *
 **/

#include "DeviceCallbacks.h"
#include "AppConfig.h"
#include "AppEvent.h"
#include "AppTask.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/server/Dnssd.h>
#include <app/util/util.h>
#include <lib/support/CodeUtils.h>

using namespace chip;
using namespace chip::Inet;
using namespace chip::System;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;

void DeviceCallbacks::DeviceEventCallback(const ChipDeviceEvent * event, intptr_t arg)
{
    switch (event->Type)
    {
    case DeviceEventType::kDnssdInitialized:
#if defined(CHIP_DEVICE_CONFIG_ENABLE_OTA_REQUESTOR)
        // Post operational event to app task
        AppEvent operationalEvent;
        operationalEvent.Type = AppEvent::kEventTyoe_DeviceOperational;
        GetAppTask().PostEvent(&operationalEvent);
#endif

        break;
    }
}

void DeviceCallbacks::PostAttributeChangeCallback(EndpointId endpointId, ClusterId clusterId, AttributeId attributeId, uint8_t type,
                                                  uint16_t size, uint8_t * value)
{
    ChipLogProgress(NotSpecified,
                    "PostAttributeChangeCallback - Cluster ID: '0x%04lx', EndPoint ID: '0x%02x', Attribute ID: '0x%04lx'",
                    clusterId, endpointId, attributeId);

    switch (clusterId)
    {
    case OnOff::Id:
        OnOnOffPostAttributeChangeCallback(endpointId, attributeId, value);
        break;

    default:
        ChipLogProgress(NotSpecified, "Unhandled cluster ID: %ld", clusterId);
        break;
    }
}

void DeviceCallbacks::OnOnOffPostAttributeChangeCallback(EndpointId endpointId, AttributeId attributeId, uint8_t * value)
{
    VerifyOrReturn(attributeId == OnOff::Attributes::OnOff::Id,
                   ChipLogProgress(NotSpecified, "Unhandled Attribute ID: '0x%04lx", attributeId));

    bool onOffState = *reinterpret_cast<bool *>(value);
    if (onOffState)
    {
        GetAppTask().TurnOnPump();
    }
    else
    {
        GetAppTask().TurnOffPump();
    }
}
