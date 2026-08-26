/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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
 * @file
 *   This file implements the handler for data model messages.
 */

#include "AppTask.h"
#include <app/clusters/identify-server/identify-server.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::Identify;
using namespace chip::app::Clusters::SoilMeasurement;

/***** Function declarations *****/
static void IdentifyStartHandler(::Identify *);
static void IdentifyStopHandler(::Identify *);

/***** Identify configuration and functions *****/
// This creates a static object of the Identify class and calls the constructor
// which registers the object and its callbacks inside the identify server
::Identify stIdentifyValve = { sWaterValveEndpointId, IdentifyStartHandler, IdentifyStopHandler,
                               IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifyPump  = { sPumpEndpointId, IdentifyStartHandler, IdentifyStopHandler, IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifySoilSensor        = { sSoilSensorEndpointId, IdentifyStartHandler, IdentifyStopHandler,
                                           IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifyThermostat        = { sThermostatEndpointId, IdentifyStartHandler, IdentifyStopHandler,
                                           IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifySmokeCO           = { sSmokeCoEndpointId, IdentifyStartHandler, IdentifyStopHandler,
                                           IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifyOccupancySensor   = { sOccupancySensorEndpointId, IdentifyStartHandler, IdentifyStopHandler,
                                           IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifyWaterLeakDetector = { sWaterLeakDetectorEndpointId, IdentifyStartHandler, IdentifyStopHandler,
                                           IdentifyTypeEnum::kVisibleIndicator };

void IdentifyStartHandler(::Identify *)
{
    AppEvent event;
    event.Type = AppEvent::kEventType_IdentifyStart;
    GetAppTask().PostEvent(&event);
}

void IdentifyStopHandler(::Identify *)
{
    AppEvent event;
    event.Type = AppEvent::kEventType_IdentifyStop;
    GetAppTask().PostEvent(&event);
}

void MatterSoilMeasurementClusterInitCallback(EndpointId endpointId)
{
    GetAppTask().InitSoilMeasurement(endpointId);
}

void MatterSoilMeasurementClusterShutdownCallback(EndpointId endpointId)
{
    GetAppTask().ShutdownSoilMeasurement(endpointId);
}

void MatterOnOffClusterInitCallback(EndpointId endpointId)
{
    GetAppTask().InitOnOff();
}

void MatterPumpConfigurationAndControlClusterInitCallback(chip::EndpointId endpointId)
{
    GetAppTask().InitPumpConfigurationAndControl();
}

void MatterSmokeCoAlarmClusterInitCallback(chip::EndpointId endpointId)
{
    GetAppTask().InitSmokeCoAlarm();
}

void EmberAfThermostatClusterInitCallback(chip::EndpointId endpointId)
{
    GetAppTask().InitThermostat(endpointId);
}
