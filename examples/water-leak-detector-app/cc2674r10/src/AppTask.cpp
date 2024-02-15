/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2020 Texas Instruments Incorporated
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
#include "DeviceCallbacks.h"
#include <app/server/Server.h>

#include "FreeRTOS.h"

#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <examples/platform/cc13x4_26x4/CC13X4_26X4DeviceAttestationCreds.h>

#include <DeviceInfoProviderImpl.h>
#include <platform/CHIPDeviceLayer.h>

#if CHIP_DEVICE_CONFIG_ENABLE_OTA_REQUESTOR
#include <app/clusters/ota-requestor/BDXDownloader.h>
#include <app/clusters/ota-requestor/DefaultOTARequestor.h>
#include <app/clusters/ota-requestor/DefaultOTARequestorDriver.h>
#include <app/clusters/ota-requestor/DefaultOTARequestorStorage.h>
#include <platform/cc13xx_26xx/OTAImageProcessorImpl.h>
#endif

#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPPlatformMemory.h>

#include <app-common/zap-generated/attributes/Accessors.h>

#include <app/clusters/identify-server/identify-server.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>

#include <ti/drivers/apps/Button.h>
#include <ti/drivers/apps/LED.h>

/* syscfg */
#include <ti_drivers_config.h>

#define CHIP_DEVICE_CONFIG_ENABLE_BOOLEAN_STATE_CONFIGURATION_TRIGGER 1

#if CHIP_DEVICE_CONFIG_ENABLE_BOOLEAN_STATE_CONFIGURATION_TRIGGER
#include <app/TestEventTriggerDelegate.h>
#include <app/clusters/boolean-state-configuration-server/BooleanStateConfigurationTestEventTriggerHandler.h>
#endif

#include <app/clusters/boolean-state-configuration-server/boolean-state-configuration-server.h>

#define APP_TASK_STACK_SIZE (4096)
#define APP_TASK_PRIORITY 4
#define APP_EVENT_QUEUE_SIZE 10

using namespace chip;
using namespace chip::Credentials;
using namespace chip::DeviceLayer;

static TaskHandle_t sAppTaskHandle;
static QueueHandle_t sAppEventQueue;

static LED_Handle sAppRedHandle;
static LED_Handle sAppGreenHandle;
static Button_Handle sAppLeftHandle;
static Button_Handle sAppRightHandle;

AppTask AppTask::sAppTask;

static const uint32_t sIdentifyBlinkRateMs = 500;

static const uint8_t sWaterLeakDetectorEndpoint = 1;

static uint8_t sTestEventTriggerEnableKey[TestEventTriggerDelegate::kEnableKeyLength] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
                                                                                          0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                                                                          0xcc, 0xdd, 0xee, 0xff };

#if CHIP_DEVICE_CONFIG_ENABLE_OTA_REQUESTOR
static DefaultOTARequestor sRequestorCore;
static DefaultOTARequestorStorage sRequestorStorage;
static DefaultOTARequestorDriver sRequestorUser;
static BDXDownloader sDownloader;
static OTAImageProcessorImpl sImageProcessor;

void InitializeOTARequestor(void)
{
    // Initialize and interconnect the Requestor and Image Processor objects
    SetRequestorInstance(&sRequestorCore);

    sRequestorStorage.Init(chip::Server::GetInstance().GetPersistentStorage());
    sRequestorCore.Init(chip::Server::GetInstance(), sRequestorStorage, sRequestorUser, sDownloader);
    sImageProcessor.SetOTADownloader(&sDownloader);
    sDownloader.SetImageProcessorDelegate(&sImageProcessor);
    sRequestorUser.Init(&sRequestorCore, &sImageProcessor);
}
#endif

int AppTask::StartAppTask()
{
    int ret = 0;

    sAppEventQueue = xQueueCreate(APP_EVENT_QUEUE_SIZE, sizeof(AppEvent));
    if (sAppEventQueue == NULL)
    {
        ChipLogProgress(NotSpecified, "Failed to allocate app event queue");
        while (true)
            ;
    }

    // Start App task.
    if (xTaskCreate(AppTaskMain, "APP", APP_TASK_STACK_SIZE / sizeof(StackType_t), NULL, APP_TASK_PRIORITY, &sAppTaskHandle) !=
        pdPASS)
    {
        ChipLogProgress(NotSpecified, "Failed to create app task");
        while (true)
            ;
    }
    return ret;
}

int AppTask::Init()
{
    LED_Params ledParams;
    Button_Params buttonParams;

    cc13xx_26xxLogInit();

    // Init Chip memory management before the stack
    Platform::MemoryInit();

    CHIP_ERROR ret = PlatformMgr().InitChipStack();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogProgress(NotSpecified, "PlatformMgr().InitChipStack() failed");
        while (true)
            ;
    }

    ret = ThreadStackMgr().InitThreadStack();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogProgress(NotSpecified, "ThreadStackMgr().InitThreadStack() failed");
        while (true)
            ;
    }

#if CHIP_DEVICE_CONFIG_THREAD_FTD
    ret = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_Router);
#elif CONFIG_OPENTHREAD_MTD_SED
    ret = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_SleepyEndDevice);
#else
    ret = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_MinimalEndDevice);
#endif

    if (ret != CHIP_NO_ERROR)
    {
        ChipLogProgress(NotSpecified, "ConnectivityMgr().SetThreadDeviceType() failed");
        while (true)
            ;
    }

    ret = ThreadStackMgrImpl().StartThreadTask();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogProgress(NotSpecified, "ThreadStackMgr().StartThreadTask() failed");
        while (true)
            ;
    }

    // Initialize LEDs
    ChipLogProgress(NotSpecified, "Initialize LEDs");
    LED_init();

    LED_Params_init(&ledParams); // default PWM LED
    sAppRedHandle = LED_open(CONFIG_LED_RED, &ledParams);
    LED_setOff(sAppRedHandle);

    LED_Params_init(&ledParams); // default PWM LED
    sAppGreenHandle = LED_open(CONFIG_LED_GREEN, &ledParams);
    LED_setOff(sAppGreenHandle);

    // Initialize buttons
    ChipLogProgress(NotSpecified, "Initialize buttons");
    Button_init();

    Button_Params_init(&buttonParams);
    buttonParams.buttonEventMask   = Button_EV_CLICKED | Button_EV_LONGPRESSED;
    buttonParams.longPressDuration = 5000U; // ms
    sAppLeftHandle                 = Button_open(CONFIG_BTN_LEFT, &buttonParams);
    Button_setCallback(sAppLeftHandle, ButtonLeftEventHandler);

    Button_Params_init(&buttonParams);
    buttonParams.buttonEventMask   = Button_EV_CLICKED | Button_EV_LONGPRESSED;
    buttonParams.longPressDuration = 5000U; // ms
    sAppRightHandle                = Button_open(CONFIG_BTN_RIGHT, &buttonParams);
    Button_setCallback(sAppRightHandle, ButtonRightEventHandler);

    // Init ZCL Data Model and start server
    ChipLogProgress(NotSpecified, "Initialize Server");
    static chip::CommonCaseDeviceServerInitParams initParams;

    // TestEventTrigger
#if CHIP_DEVICE_CONFIG_ENABLE_BOOLEAN_STATE_CONFIGURATION_TRIGGER
    static SimpleTestEventTriggerDelegate sTestEventTriggerDelegate{};
    static BooleanStateConfigurationTestEventTriggerHandler sBooleanStateConfigurationTestEventTriggerHandler{};
    VerifyOrDie(sTestEventTriggerDelegate.Init(ByteSpan(sTestEventTriggerEnableKey)) == CHIP_NO_ERROR);
    VerifyOrDie(sTestEventTriggerDelegate.AddHandler(&sBooleanStateConfigurationTestEventTriggerHandler) == CHIP_NO_ERROR);
    initParams.testEventTriggerDelegate = &sTestEventTriggerDelegate;
#endif

    // Init ZCL Data Model
    (void) initParams.InitializeStaticResourcesBeforeServerInit();
    chip::Server::GetInstance().Init(initParams);

    // Initialize device attestation config
#ifdef CC13X2_26X2_ATTESTATION_CREDENTIALS
    SetDeviceAttestationCredentialsProvider(CC13X2_26X2::GetCC13X2_26X2DacProvider());
#else
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());
#endif

    ConfigurationMgr().LogDeviceConfig();

    // QR code will be used with CHIP Tool
    PrintOnboardingCodes(RendezvousInformationFlags(RendezvousInformationFlag::kBLE));

    // Init event callback and start event loop
    InitDeviceEventCallback();
    ret = PlatformMgr().StartEventLoopTask();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogProgress(NotSpecified, "PlatformMgr().StartEventLoopTask() failed");
        while (1)
            ;
    }

    return 0;
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;

    sAppTask.Init();

    while (true)
    {
        /* Task pend until we have stuff to do */
        if (xQueueReceive(sAppEventQueue, &event, portMAX_DELAY) == pdTRUE)
        {
            sAppTask.DispatchEvent(&event);
        }
    }
}

void AppTask::PostEvent(const AppEvent * aEvent)
{
    if (xQueueSend(sAppEventQueue, aEvent, 0) != pdPASS)
    {
        /* Failed to post the message */
    }
}

void AppTask::ButtonLeftEventHandler(Button_Handle handle, Button_EventMask events)
{
    AppEvent event;
    event.Type = AppEvent::kEventType_ButtonLeft;

    if (events & Button_EV_CLICKED)
    {
        event.ButtonEvent.Type = AppEvent::kAppEventButtonType_Clicked;
    }
    else if (events & Button_EV_LONGPRESSED)
    {
        event.ButtonEvent.Type = AppEvent::kAppEventButtonType_LongPressed;
    }
    // button callbacks are in ISR context
    if (xQueueSendFromISR(sAppEventQueue, &event, NULL) != pdPASS)
    {
        /* Failed to post the message */
    }
}

void AppTask::ButtonRightEventHandler(Button_Handle handle, Button_EventMask events)
{
    AppEvent event;
    event.Type = AppEvent::kEventType_ButtonRight;

    if (events & Button_EV_CLICKED)
    {
        event.ButtonEvent.Type = AppEvent::kAppEventButtonType_Clicked;
    }
    else if (events & Button_EV_LONGPRESSED)
    {
        event.ButtonEvent.Type = AppEvent::kAppEventButtonType_LongPressed;
    }
    // button callbacks are in ISR context
    if (xQueueSendFromISR(sAppEventQueue, &event, NULL) != pdPASS)
    {
        /* Failed to post the message */
    }
}

void AppTask::DispatchEvent(AppEvent * aEvent)
{
    switch (aEvent->Type)
    {
    case AppEvent::kEventType_ButtonLeft:
        if (AppEvent::kAppEventButtonType_Clicked == aEvent->ButtonEvent.Type)
        {
            // Enable BLE advertisements
            if (!ConnectivityMgr().IsBLEAdvertisingEnabled())
            {
                if (Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow() == CHIP_NO_ERROR)
                {
                    ChipLogProgress(NotSpecified, "Enabled BLE Advertisement");
                }
                else
                {
                    ChipLogProgress(NotSpecified, "OpenBasicCommissioningWindow() failed");
                }
            }
            // Disable BLE advertisements
            else // if (ConnectivityMgr().IsBLEAdvertisingEnabled())
            {
                ConnectivityMgr().SetBLEAdvertisingEnabled(false);
                ChipLogProgress(NotSpecified, "Disabled BLE Advertisements");
            }
        }
        else if (AppEvent::kAppEventButtonType_LongPressed == aEvent->ButtonEvent.Type)
        {
            // Factory reset
            chip::Server::GetInstance().ScheduleFactoryReset();
        }
        break;

    case AppEvent::kEventType_ButtonRight:
        if (AppEvent::kAppEventButtonType_Clicked == aEvent->ButtonEvent.Type)
        {
            chip::DeviceLayer::PlatformMgr().ScheduleWork(ToogleBooleanState);
        }
        else if (AppEvent::kAppEventButtonType_LongPressed == aEvent->ButtonEvent.Type)
        {
            // No action
        }
        break;

    case AppEvent::kEventType_IdentifyStart:
        LED_setOn(sAppGreenHandle, LED_BRIGHTNESS_MAX);
        LED_startBlinking(sAppGreenHandle, sIdentifyBlinkRateMs, LED_BLINK_FOREVER);
        ChipLogProgress(NotSpecified, "Identify started");
        break;

    case AppEvent::kEventType_IdentifyStop:
        LED_stopBlinking(sAppGreenHandle);
        LED_setOff(sAppGreenHandle);
        ChipLogProgress(NotSpecified, "Identify stopped");
        break;

    case AppEvent::kEventTyoe_DeviceOperational:
#if CHIP_DEVICE_CONFIG_ENABLE_OTA_REQUESTOR
        InitializeOTARequestor();
#endif
        break;

    case AppEvent::kEventType_AppEvent:
        if (NULL != aEvent->Handler)
        {
            aEvent->Handler(aEvent);
        }
        break;

    case AppEvent::kEventType_None:
    default:
        break;
    }
}

void AppTask::ToogleBooleanState(intptr_t arg)
{
    bool attributeValue;
    chip::app::Clusters::BooleanState::Attributes::StateValue::Get(sWaterLeakDetectorEndpoint, &attributeValue);
    ChipLogProgress(NotSpecified, "Toggle boolean state: %d -> %d", attributeValue, !attributeValue);

    if ((!attributeValue) == true)
    {
        LeakDetectorTrigger();
    }
    else
    {
        LeakDetectorUntrigger();
    }
}

void AppTask::LeakDetectorTrigger(void)
{
    BitMask<chip::app::Clusters::BooleanStateConfiguration::AlarmModeBitmap> alarmsEnabled;
    chip::app::Clusters::BooleanStateConfiguration::Attributes::AlarmsEnabled::Get(sWaterLeakDetectorEndpoint, &alarmsEnabled);

    if (alarmsEnabled.Has(chip::app::Clusters::BooleanStateConfiguration::AlarmModeBitmap::kVisual))
    {
        LED_setOn(sAppRedHandle, LED_BRIGHTNESS_MAX);
    }
    else
    {
        ChipLogProgress(NotSpecified, "Visual alarming is disabled, LED not turned on");
    }

    chip::app::Clusters::BooleanState::Attributes::StateValue::Set(sWaterLeakDetectorEndpoint, true);
    chip::app::Clusters::BooleanStateConfiguration::SetAllEnabledAlarmsActive(sWaterLeakDetectorEndpoint);
}

void AppTask::LeakDetectorUntrigger(void)
{
    LED_setOff(sAppRedHandle);
    chip::app::Clusters::BooleanState::Attributes::StateValue::Set(sWaterLeakDetectorEndpoint, false);
    chip::app::Clusters::BooleanStateConfiguration::ClearAllAlarms(sWaterLeakDetectorEndpoint);
}
