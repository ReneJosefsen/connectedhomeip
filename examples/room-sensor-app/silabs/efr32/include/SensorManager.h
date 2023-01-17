/*
 *
 *    Copyright (c) 2019 Google LLC.
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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "AppEvent.h"

#include "FreeRTOS.h"
#include "timers.h" // provides FreeRTOS timer support

#include <lib/core/CHIPError.h>

class SensorManager
{
public:
    enum Action_t
    {
        ON_ACTION = 0,
        OFF_ACTION,

        INVALID_ACTION
    } Action;

    enum State_t
    {
        kState_OffInitiated = 0,
        kState_OffCompleted,
        kState_OnInitiated,
        kState_OnCompleted,
    } State;

    CHIP_ERROR Init();

private:
    struct stSensorMeasurements
    {
        uint32_t rh    = 0;
        int32_t temp   = 0;
        float lux      = 0.0;
        float pressure = 0.0;
        float temp2    = 0.0;
    };

    friend SensorManager & SensorMgr(void);

    void CancelTimer(void);
    static void TimerEventHandler(TimerHandle_t xTimer);
    static void UpdateClusterState(intptr_t context);

    static SensorManager sSensor;
};

inline SensorManager & SensorMgr(void)
{
    return SensorManager::sSensor;
}
