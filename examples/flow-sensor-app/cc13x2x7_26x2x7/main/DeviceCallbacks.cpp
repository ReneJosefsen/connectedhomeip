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

using namespace chip;
using namespace chip::DeviceLayer;

/***** Helper functions *****/
void DeviceEventHandler(const ChipDeviceEvent * event, intptr_t arg)
{
    switch (event->Type)
    {
    case DeviceEventType::kDnssdPlatformInitialized:
#if defined(CHIP_DEVICE_CONFIG_ENABLE_OTA_REQUESTOR)
        // Post operational event to app task
        AppEvent operationalEvent;
        operationalEvent.Type = AppEvent::kEventTyoe_DeviceOperational;
        AppTask::GetAppTask().PostEvent(&operationalEvent);
#endif

        break;
    }
}

/***** Public functions *****/
void InitDeviceEventCallback(void)
{
    // Initialize the DeviceCallbacks to captures the Matter events
    // Register a function to receive events from the CHIP device layer.  Note that calls to
    // this function will happen on the CHIP event loop thread, not the app_main thread.
    PlatformMgr().AddEventHandler(DeviceEventHandler, reinterpret_cast<intptr_t>(nullptr));
}
