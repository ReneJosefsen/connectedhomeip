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

#include "AppTask.h"
#include "AppConfig.h"
#include "AppEvent.h"

#ifdef ENABLE_WSTK_LEDS
#include "LEDWidget.h"
#include "sl_simple_led_instances.h"
#endif // ENABLE_WSTK_LEDS

#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>

#include <assert.h>

#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <lib/support/CodeUtils.h>

#include <platform/CHIPDeviceLayer.h>

#ifdef ENABLE_WSTK_LEDS
#define SYSTEM_STATE_LED &sl_led_led0
#define PUMP_LED &sl_led_led1
#endif // ENABLE_WSTK_LEDS

#define APP_FUNCTION_BUTTON &sl_button_btn0
#define APP_PUMP_SWITCH &sl_button_btn1

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::TLV;

#ifdef ENABLE_WSTK_LEDS
LEDWidget sPumpLED;
TimerHandle_t sPumpLEDTimer;
#endif // ENABLE_WSTK_LEDS

extern ::Identify stIdentify;

AppTask AppTask::sAppTask;

CHIP_ERROR AppTask::Init()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
#ifdef DISPLAY_ENABLED
    GetLCD().Init((uint8_t *) "Pump-App");
#endif

    err = BaseApplication::Init(&stIdentify);
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("BaseApplication::Init() failed");
        appError(err);
    }

    PumpMgr().Init();
    PumpMgr().SetCallbacks(ActionInitiated, ActionCompleted);

#ifdef ENABLE_WSTK_LEDS
    sPumpLED.Init(PUMP_LED);

    // Create FreeRTOS sw timer for Pump LED Management.
    sPumpLEDTimer = xTimerCreate("PumpLedTmr",            // Text Name
                                 10,                      // Default timer period (mS)
                                 true,                    // reload timer
                                 (void *) this,           // Timer Id
                                 PumpLEDTimerEventHandler // Timer callback handler
    );
    if (sPumpLEDTimer == NULL)
    {
        SILABS_LOG("Pump LED Timer create failed");
        appError(APP_ERROR_CREATE_TIMER_FAILED);
    }
#endif // ENABLE_WSTK_LEDS

    return err;
}

CHIP_ERROR AppTask::StartAppTask()
{
    return BaseApplication::StartAppTask(AppTaskMain);
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;
    QueueHandle_t sAppEventQueue = *(static_cast<QueueHandle_t *>(pvParameter));

    CHIP_ERROR err = sAppTask.Init();
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("AppTask.Init() failed");
        appError(err);
    }

#if !(defined(CHIP_DEVICE_CONFIG_ENABLE_SED) && CHIP_DEVICE_CONFIG_ENABLE_SED)
    sAppTask.StartStatusLEDTimer();
#endif

    SILABS_LOG("App Task started");

    while (true)
    {
        BaseType_t eventReceived = xQueueReceive(sAppEventQueue, &event, portMAX_DELAY);
        while (eventReceived == pdTRUE)
        {
            sAppTask.DispatchEvent(&event);
            eventReceived = xQueueReceive(sAppEventQueue, &event, 0);
        }
    }
}

void AppTask::IdentifyActionEventHandler(AppEvent * aEvent)
{
    switch (aEvent->Type)
    {
    case AppEvent::kEventType_IdentifyStart:
#if CHIP_DEVICE_CONFIG_ENABLE_SED == 1
        sAppTask.StartStatusLEDTimer();
#endif
        break;

    case AppEvent::kEventType_IdentifyStop:
#if CHIP_DEVICE_CONFIG_ENABLE_SED == 1
        sAppTask.StopStatusLEDTimer();
#endif
        break;

    default:
        // No action
        break;
    }
}

void AppTask::PumpActionEventHandler(AppEvent * aEvent)
{
    // Toggle Pump state
    if (!PumpMgr().IsStopped())
    {
        PumpMgr().InitiateAction(AppEvent::kEventType_Button, PumpManager::STOP_ACTION);
    }
    else
    {
        PumpMgr().InitiateAction(AppEvent::kEventType_Button, PumpManager::START_ACTION);
    }
}

void AppTask::ButtonEventHandler(const sl_button_t * buttonHandle, uint8_t btnAction)
{
    if (buttonHandle == NULL)
    {
        return;
    }

    AppEvent button_event           = {};
    button_event.Type               = AppEvent::kEventType_Button;
    button_event.ButtonEvent.Action = btnAction;

    if (buttonHandle == APP_PUMP_SWITCH && btnAction == SL_SIMPLE_BUTTON_PRESSED)
    {
        button_event.Handler = PumpActionEventHandler;
        sAppTask.PostEvent(&button_event);
    }
    else if (buttonHandle == APP_FUNCTION_BUTTON)
    {
        button_event.Handler = BaseApplication::ButtonHandler;
        sAppTask.PostEvent(&button_event);
    }
}

void AppTask::ActionInitiated(PumpManager::Action_t aAction, int32_t aActor)
{
    // If the action has been initiated by the pump, update the pump trait
    // and start flashing the LEDs rapidly to indicate action initiation.
    if (aAction == PumpManager::START_ACTION)
    {
        SILABS_LOG("Pump start initiated");
        ; // TODO
    }
    else if (aAction == PumpManager::STOP_ACTION)
    {
        SILABS_LOG("Pump stop initiated");
        ; // TODO
    }

#ifdef ENABLE_WSTK_LEDS
    xTimerStart(sPumpLEDTimer, 0);
    sPumpLED.Blink(50);
#endif // ENABLE_WSTK_LEDS
}

void AppTask::ActionCompleted(PumpManager::Action_t aAction, int32_t aActor)
{
#ifdef ENABLE_WSTK_LEDS
    xTimerStop(sPumpLEDTimer, 100);
#endif // ENABLE_WSTK_LEDS

    // if the action has been completed by the pump, update the pump trait.
    // Turn on the pump state LED if in a STARTED state OR
    // Turn off the pump state LED if in an STOPPED state.
    if (aAction == PumpManager::START_ACTION)
    {
        SILABS_LOG("Pump start completed");

#ifdef ENABLE_WSTK_LEDS
        sPumpLED.Set(true);
#endif // ENABLE_WSTK_LEDS
    }
    else if (aAction == PumpManager::STOP_ACTION)
    {
        SILABS_LOG("Pump stop completed");

#ifdef ENABLE_WSTK_LEDS
        sPumpLED.Set(false);
#endif // ENABLE_WSTK_LEDS
    }

    if (aActor == AppEvent::kEventType_Button)
    {
        // We must ensure that the Cluster accessors gets called in the right context
        // which is the Matter mainloop thru ScheduleWork()
        PlatformMgr().ScheduleWork(AppTask::UpdateClusterState, reinterpret_cast<intptr_t>(nullptr));
    }
}

void AppTask::UpdateClusterState(intptr_t context)
{
    // Set On/Off state
    EmberStatus status;
    bool onOffState = !PumpMgr().IsStopped();
    status          = chip::app::Clusters::OnOff::Attributes::OnOff::Set(ONOFF_CLUSTER_ENDPOINT, onOffState);
    if (status != EMBER_ZCL_STATUS_SUCCESS)
    {
        ChipLogError(NotSpecified, "ERR: Updating On/Off state  %x", status);
    }
}

void AppTask::PumpLEDTimerEventHandler(TimerHandle_t xTimer)
{
#ifdef ENABLE_WSTK_LEDS
    sPumpLED.Animate();
#endif // ENABLE_WSTK_LEDS
}
