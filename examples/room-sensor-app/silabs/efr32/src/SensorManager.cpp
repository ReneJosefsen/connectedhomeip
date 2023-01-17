/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

#include <cmath>

#include "AppConfig.h"
#include "AppTask.h"
#include "SensorManager.h"
#include <FreeRTOS.h>

#include <app-common/zap-generated/attributes/Accessors.h>
#include <lib/support/CHIPMem.h>
#include <platform/CHIPDeviceLayer.h>

extern "C" {
#include "sl_board_control.h"
#include "sl_i2cspm_instances.h"
#include "sl_pressure.h"
#include "sl_sensor_lux.h"
#include "sl_sensor_pressure.h"
#include "sl_sensor_rht.h"
#include "sl_sensor_select.h"
}

#define TEMPERATURE_SENSOR_ENDPOINT 1
#define HUMIDITY_SENSOR_ENDPOINT 2
#define LIGHT_SENSOR_ENDPOINT 3
#define PRESSURE_SENSOR_ENDPOINT 4

#define PRINT_MEASSUREMENTS 1

using namespace chip;
using namespace chip::app;
using namespace chip::DeviceLayer;

SensorManager SensorManager::sSensor;

TimerHandle_t sSensorTimer;

#if PRINT_MEASSUREMENTS
static float meassurementFloat = 0.0;
static int meassurementInt     = 0;
static int meassurementFrac    = 0;
#endif

CHIP_ERROR SensorManager::Init()
{
    // Init sensors
    sl_board_enable_sensor(SL_BOARD_SENSOR_RHT);

    sl_i2cspm_init_instances();
    sl_sensor_rht_init();
    sl_sensor_pressure_init();
    sl_sensor_lux_init();

    // Create FreeRTOS sw timer for sensor timer.
    sSensorTimer = xTimerCreate("sensorTmr",      // Just a text name, not used by the RTOS kernel
                                10 * 1000,        // == default timer period (mS)
                                true,             // no timer reload (==one-shot)
                                (void *) this,    // init timer id = sensor obj context
                                TimerEventHandler // timer callback handler
    );

    if (sSensorTimer == NULL)
    {
        SILABS_LOG("sSensorTimer timer create failed");
        return APP_ERROR_CREATE_TIMER_FAILED;
    }

    xTimerStart(sSensorTimer, 0);

    return CHIP_NO_ERROR;
}

void SensorManager::CancelTimer(void)
{
    if (xTimerStop(sSensorTimer, 0) == pdFAIL)
    {
        SILABS_LOG("sSensorTimer stop() failed");
        appError(APP_ERROR_STOP_TIMER_FAILED);
    }
}

void SensorManager::TimerEventHandler(TimerHandle_t xTimer)
{
    stSensorMeasurements * sensorMeasurements = chip::Platform::New<stSensorMeasurements>();

    sl_sensor_rht_get(&sensorMeasurements->rh, &sensorMeasurements->temp);
    sl_sensor_lux_get(&sensorMeasurements->lux);

    // There is a bug in sl_sensor_pressure_get, meaning that it actually returns
    // the temp and not pressure so the direct API is used instead.
    // sl_sensor_pressure_get(&pressure);
    sl_pressure_measure_pressure(sl_sensor_select(SL_BOARD_SENSOR_PRESSURE), &sensorMeasurements->pressure);
    sl_pressure_measure_temperature(sl_sensor_select(SL_BOARD_SENSOR_PRESSURE), &sensorMeasurements->temp2);

#if PRINT_MEASSUREMENTS
    meassurementFloat = (float) sensorMeasurements->temp / 1000.00;
    meassurementInt   = meassurementFloat;
    meassurementFrac  = sensorMeasurements->temp - (meassurementInt * 1000);
    SILABS_LOG("Temp: %ld - %d.%03d C", sensorMeasurements->temp, meassurementInt, meassurementFrac);

    meassurementInt  = sensorMeasurements->temp2;
    meassurementFrac = (sensorMeasurements->temp2 - meassurementInt) * 1000;
    SILABS_LOG("Temp2: %d.%03d C", meassurementInt, meassurementFrac);

    meassurementFloat = (float) sensorMeasurements->rh / 1000.00;
    meassurementInt   = meassurementFloat;
    meassurementFrac  = sensorMeasurements->rh - (meassurementInt * 1000);
    SILABS_LOG("Humidity: %lu - %d.%03d %%", sensorMeasurements->rh, meassurementInt, meassurementFrac);

    meassurementInt  = sensorMeasurements->lux;
    meassurementFrac = (sensorMeasurements->lux - meassurementInt) * 1000;
    SILABS_LOG("Light: %d.%03d Lux", meassurementInt, meassurementFrac);

    meassurementInt  = sensorMeasurements->pressure;
    meassurementFrac = (sensorMeasurements->pressure - meassurementInt) * 1000;
    SILABS_LOG("Pressure: %d.%03d Pa", meassurementInt, meassurementFrac);
#endif

    // We must ensure that the Cluster accessors gets called in the right context
    // which is the Matter mainloop thru ScheduleWork()
    PlatformMgr().ScheduleWork(SensorManager::UpdateClusterState, reinterpret_cast<intptr_t>(sensorMeasurements));
}

void SensorManager::UpdateClusterState(intptr_t context)
{
    // Get the received sensor data
    stSensorMeasurements * sensorMeasurements = reinterpret_cast<stSensorMeasurements *>(context);

    // SILABS_LOG("Temp: %d", sensorMeasurements->temp);
    // SILABS_LOG("Temp2: %d", (int32_t)(sensorMeasurements->temp2 * 1000));
    // SILABS_LOG("Humidity: %d", sensorMeasurements->rh);
    // SILABS_LOG("Light: %d", (uint32_t)(sensorMeasurements->lux * 1000));
    // SILABS_LOG("Pressure: %d", (int32_t)(sensorMeasurements->pressure * 1000));

    // Convert to values that fits the clusters
    int16_t tempAverage                    = (sensorMeasurements->temp + (int32_t)(sensorMeasurements->temp2 * 1000)) / 2;
    int16_t temperatureMeasuredValue       = tempAverage / 10;
    uint16_t relativeHumidityMeasuredValue = (sensorMeasurements->rh / 10);
    uint16_t illuminanceMeasuredValue      = (uint16_t)((10000.0f * log10(sensorMeasurements->lux)) + 1);
    int16_t pressureMeasuredValue          = (int16_t) round((sensorMeasurements->pressure) / 100);

#if PRINT_MEASSUREMENTS
    SILABS_LOG("temperatureMeasuredValue: %d", temperatureMeasuredValue);
    SILABS_LOG("relativeHumidityMeasuredValue: %d", relativeHumidityMeasuredValue);
    SILABS_LOG("illuminanceMeasuredValue: %d", illuminanceMeasuredValue);
    SILABS_LOG("pressureMeasuredValue: %d", pressureMeasuredValue);
#endif

    // Adjust the MeasuredValue attributes to the new meassurements
    Clusters::TemperatureMeasurement::Attributes::MeasuredValue::Set(TEMPERATURE_SENSOR_ENDPOINT, temperatureMeasuredValue);
    Clusters::RelativeHumidityMeasurement::Attributes::MeasuredValue::Set(HUMIDITY_SENSOR_ENDPOINT, relativeHumidityMeasuredValue);
    Clusters::IlluminanceMeasurement::Attributes::MeasuredValue::Set(LIGHT_SENSOR_ENDPOINT, illuminanceMeasuredValue);
    Clusters::PressureMeasurement::Attributes::MeasuredValue::Set(PRESSURE_SENSOR_ENDPOINT, pressureMeasuredValue);

    // Any allocated data is deleted to avoid a leak
    if (sensorMeasurements != nullptr)
    {
        chip::Platform::Delete(sensorMeasurements);
    }
}
