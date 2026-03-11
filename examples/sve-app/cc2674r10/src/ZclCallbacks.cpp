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

/***** Variables *****/
static const chip::EndpointId sValveEndpointId      = 1;
static const chip::EndpointId sPumpEndpointId       = 2;
static const chip::EndpointId sSoilSensorEndpointId = 3;
static const chip::EndpointId sThermostatEndpointId = 4;
static const chip::EndpointId sSmokeCoEndpointId    = 5;

/***** Identify configuration and functions *****/
// This creates a static object of the Identify class and calls the constructor
// which registers the object and its callbacks inside the identify server
::Identify stIdentifyValve = { sValveEndpointId, IdentifyStartHandler, IdentifyStopHandler, IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifyPump  = { sPumpEndpointId, IdentifyStartHandler, IdentifyStopHandler, IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifySoilSensor = { sSoilSensorEndpointId, IdentifyStartHandler, IdentifyStopHandler,
                                    IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifyThermostat = { sThermostatEndpointId, IdentifyStartHandler, IdentifyStopHandler,
                                    IdentifyTypeEnum::kVisibleIndicator };
::Identify stIdentifySmokeCO    = { sSmokeCoEndpointId, IdentifyStartHandler, IdentifyStopHandler,
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

void emberAfSoilMeasurementClusterInitCallback(EndpointId endpointId)
{
    GetAppTask().InitSoilMeasurement(endpointId);
}

void emberAfSoilMeasurementClusterShutdownCallback(EndpointId endpointId)
{
    GetAppTask().ShutdownSoilMeasurement(endpointId);
}

void emberAfOnOffClusterInitCallback(EndpointId endpointId)
{
    GetAppTask().InitOnOff();
}

void emberAfPumpConfigurationAndControlClusterInitCallback(chip::EndpointId endpointId)
{
    GetAppTask().InitPumpConfigurationAndControl();
}

void emberAfSmokeCoAlarmClusterInitCallback(chip::EndpointId endpointId)
{
    GetAppTask().InitSmokeCoAlarm();
}

void emberAfThermostatClusterInitCallback(chip::EndpointId endpointId)
{
    GetAppTask().InitThermostat(endpointId);
}
