/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

extern "C" {
#include "ti_drivers_config.h"
#ifdef ti_log_Log_ENABLE
#include "ti_log_config.h"
#endif
}

#include "AppEvent.h"
#include "AppTask.h"
#include "DeviceCallbacks.h"
#include <AppConfig.h>

#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <examples/platform/ti/TIDeviceAttestationCreds.h>

#include <DeviceInfoProviderImpl.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/DiagnosticDataProvider.h>

#if CHIP_DEVICE_CONFIG_ENABLE_OTA_REQUESTOR
#include <app/clusters/ota-requestor/BDXDownloader.h>
#include <app/clusters/ota-requestor/DefaultOTARequestor.h>
#include <app/clusters/ota-requestor/DefaultOTARequestorDriver.h>
#include <app/clusters/ota-requestor/DefaultOTARequestorStorage.h>
#include <platform/ti/cc13xx_26xx/OTAImageProcessorImpl.h>
#endif

#include <inet/EndPointStateOpenThread.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPPlatformMemory.h>

#include <app-common/zap-generated/attributes/Accessors.h>

#include <app/clusters/identify-server/identify-server.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <data-model-providers/codegen/Instance.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <app/clusters/network-commissioning/network-commissioning.h>
#include <platform/OpenThread/GenericNetworkCommissioningThreadDriver.h>

#include <ti/drivers/apps/Button.h>
#include <ti/drivers/apps/LED.h>

/* syscfg */
#include <ti_drivers_config.h>

#include "ValveControlDelegate.h"
#include <app/clusters/soil-measurement-server/soil-measurement-cluster.h>
#include <app/clusters/valve-configuration-and-control-server/valve-configuration-and-control-server.h>

#include <app/InteractionModelEngine.h>
#include <app/data-model-provider/MetadataTypes.h>

#define APP_TASK_STACK_SIZE (4096)
#define APP_TASK_PRIORITY 4
#define APP_EVENT_QUEUE_SIZE 10

using namespace chip;
using namespace chip::app;
using namespace chip::Credentials;
using namespace chip::DeviceLayer;
using namespace chip::DeviceManager;

static TaskHandle_t sAppTaskHandle;
static QueueHandle_t sAppEventQueue;

static LED_Handle sAppRedHandle;
static LED_Handle sAppGreenHandle;

static Button_Handle sAppLeftHandle;
static Button_Handle sAppRightHandle;

static DeviceInfoProviderImpl sExampleDeviceInfoProvider;

Clusters::NetworkCommissioning::InstanceAndDriver<NetworkCommissioning::GenericThreadDriver> sThreadNetworkDriver(0 /*endpointId*/);

AppTask AppTask::sAppTask;

static DeviceCallbacks DeviceEventCallbacks;

static const uint32_t sIdentifyBlinkRateMs = 500;

static const uint8_t sWaterValveEndpoint = 1;
static Clusters::ValveConfigurationAndControl::ValveControlDelegate sValveDelegate;

static const uint8_t sPumpEndpoint = 2;

static LazyRegisteredServerCluster<Clusters::SoilMeasurementCluster> gSoilMeasurementServer;
static TimerHandle_t sSoilMeasurementTimer = 0;

static const Clusters::Globals::Structs::MeasurementAccuracyRangeStruct::Type
    kDefaultSoilMoistureMeasurementLimitsAccuracyRange[] = {
        { .rangeMin = 0, .rangeMax = 100, .percentMax = MakeOptional(static_cast<Percent100ths>(10)) }
    };

const Clusters::SoilMeasurement::Attributes::SoilMoistureMeasurementLimits::TypeInfo::Type kDefaultSoilMoistureMeasurementLimits = {
    .measurementType  = Clusters::Globals::MeasurementTypeEnum::kSoilMoisture,
    .measured         = true,
    .minMeasuredValue = 0,
    .maxMeasuredValue = 100,
    .accuracyRanges   = DataModel::List<const Clusters::Globals::Structs::MeasurementAccuracyRangeStruct::Type>(
        kDefaultSoilMoistureMeasurementLimitsAccuracyRange)
};

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

    sRequestorStorage.Init(Server::GetInstance().GetPersistentStorage());
    sRequestorCore.Init(Server::GetInstance(), sRequestorStorage, sRequestorUser, sDownloader);
    sImageProcessor.SetOTADownloader(&sDownloader);
    sDownloader.SetImageProcessorDelegate(&sImageProcessor);
    sRequestorUser.Init(&sRequestorCore, &sImageProcessor);
}
#endif

void AppTask::SoilMeasurementTimerEventHandler(TimerHandle_t xTimer)
{
    DeviceLayer::PlatformMgr().ScheduleWork(TakeSoilMeasurement);
}

void AppTask::InitSoilMeasurement(EndpointId endpointId)
{
    gSoilMeasurementServer.Create(endpointId, kDefaultSoilMoistureMeasurementLimits);

    CHIP_ERROR err = CodegenDataModelProvider::Instance().Registry().Register(gSoilMeasurementServer.Registration());
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "SoilMeasurement cluster error registration");
    }
}

void AppTask::ShutdownSoilMeasurement(EndpointId endpointId)
{
    CHIP_ERROR err = CodegenDataModelProvider::Instance().Registry().Unregister(&gSoilMeasurementServer.Cluster());
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "SoilMeasurement unregister error");
    }

    gSoilMeasurementServer.Destroy();
}

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
#elif CHIP_CONFIG_ENABLE_ICD_SERVER
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

    sThreadNetworkDriver.Init();
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
    buttonParams.buttonEventMask   = Button_EV_CLICKED | Button_EV_DOUBLECLICKED | Button_EV_LONGPRESSED;
    buttonParams.longPressDuration = 5000U; // ms
    sAppLeftHandle                 = Button_open(CONFIG_BTN_LEFT, &buttonParams);
    Button_setCallback(sAppLeftHandle, ButtonLeftEventHandler);

    Button_Params_init(&buttonParams);
    buttonParams.buttonEventMask   = Button_EV_CLICKED | Button_EV_DOUBLECLICKED | Button_EV_LONGPRESSED;
    buttonParams.longPressDuration = 5000U; // ms
    sAppRightHandle                = Button_open(CONFIG_BTN_RIGHT, &buttonParams);
    Button_setCallback(sAppRightHandle, ButtonRightEventHandler);

    // Initialize device attestation config
#ifdef TI_ATTESTATION_CREDENTIALS
#ifdef TI_FACTORY_DATA
    SetDeviceInstanceInfoProvider(&mFactoryDataProvider);
    SetDeviceAttestationCredentialsProvider(&mFactoryDataProvider);
    SetCommissionableDataProvider(&mFactoryDataProvider);

    // Workaround to make the BLE stack set the proper device name on initialization
    uint16_t deviceDiscriminator = 0;
    GetCommissionableDataProvider()->GetSetupDiscriminator(deviceDiscriminator);
    char deviceName[GAP_DEVICE_NAME_LEN + 1] = { 0 };
    snprintf(deviceName, GAP_DEVICE_NAME_LEN, "%s%04u", CHIP_DEVICE_CONFIG_BLE_DEVICE_NAME_PREFIX, deviceDiscriminator);
    ConnectivityMgr().SetBLEDeviceName(deviceName);
    ChipLogProgress(NotSpecified, "BLE: Set device name to %s", deviceName);
#else
    SetDeviceAttestationCredentialsProvider(TI::GetTIDacProvider());
#endif
#else
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());
#endif

    // Init ZCL Data Model and start server
    ChipLogProgress(NotSpecified, "Initialize Server");
    static CommonCaseDeviceServerInitParams initParams;

    (void) initParams.InitializeStaticResourcesBeforeServerInit();
    initParams.dataModelProvider = CodegenDataModelProviderInstance(initParams.persistentStorageDelegate);

    Inet::EndPointStateOpenThread::OpenThreadEndpointInitParam nativeParams;
    nativeParams.lockCb                = [] { ThreadStackMgr().LockThreadStack(); };
    nativeParams.unlockCb              = [] { ThreadStackMgr().UnlockThreadStack(); };
    nativeParams.openThreadInstancePtr = DeviceLayer::ThreadStackMgrImpl().OTInstance();
    initParams.endpointNativeParams    = static_cast<void *>(&nativeParams);

    // Initialize info provider
    sExampleDeviceInfoProvider.SetStorageDelegate(initParams.persistentStorageDelegate);
    SetDeviceInfoProvider(&sExampleDeviceInfoProvider);

    Server::GetInstance().Init(initParams);

    ConfigurationMgr().LogDeviceConfig();

    // QR code will be used with CHIP Tool
    // The payload is printed "manually" since the utility print function assumes
    // STANDARD commissioning flow, which is not valid in our case.
    PayloadContents payload;
    RendezvousInformationFlags aRendezvousFlags = RendezvousInformationFlags(RendezvousInformationFlag::kBLE);

    ret = GetPayloadContents(payload, aRendezvousFlags);
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "GetPayloadContents() failed: %" CHIP_ERROR_FORMAT, ret.Format());
    }

    payload.commissioningFlow = CommissioningFlow::kUserActionRequired;

    PrintOnboardingCodes(payload);

    // Init event callback and start event loop
    ChipLogProgress(NotSpecified, "Start CHIPDeviceManager and Start Event Loop Task");
    CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();
    ret                           = deviceMgr.Init(&DeviceEventCallbacks);
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogProgress(NotSpecified, "CHIPDeviceManager::Init() failed: %s", ErrorStr(ret));
        while (1)
            ;
    }

    // Set ValveConfigurationAndControl delegate
    Clusters::ValveConfigurationAndControl::SetDefaultDelegate(EndpointId(sWaterValveEndpoint), &sValveDelegate);

    // Start timer to make soil measurement every 1 second
    sSoilMeasurementTimer = xTimerCreate("SoilMeasTmr",                   // Just a text name, not used by the RTOS kernel
                                         30 * 1000,                       // timer period (mS)
                                         true,                            // no timer reload (==one-shot)
                                         (void *) this,                   // init timer id = light obj context
                                         SoilMeasurementTimerEventHandler // timer callback handler
    );
    xTimerStart(sSoilMeasurementTimer, 0);

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
    else if (events & Button_EV_DOUBLECLICKED)
    {
        event.ButtonEvent.Type = AppEvent::kAppEventButtonType_DoubleClicked;
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
    else if (events & Button_EV_DOUBLECLICKED)
    {
        event.ButtonEvent.Type = AppEvent::kAppEventButtonType_DoubleClicked;
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
        else if (AppEvent::kAppEventButtonType_DoubleClicked == aEvent->ButtonEvent.Type)
        {
            DeviceLayer::PlatformMgr().ScheduleWork(ChangeConfigutation);
        }
        else if (AppEvent::kAppEventButtonType_LongPressed == aEvent->ButtonEvent.Type)
        {
            // Factory reset
            Server::GetInstance().ScheduleFactoryReset();
        }
        break;

    case AppEvent::kEventType_ButtonRight:
        if (AppEvent::kAppEventButtonType_Clicked == aEvent->ButtonEvent.Type)
        {
            DeviceLayer::PlatformMgr().ScheduleWork(TakeSoilMeasurement);
        }
        else if (AppEvent::kAppEventButtonType_DoubleClicked == aEvent->ButtonEvent.Type)
        {
            DeviceLayer::PlatformMgr().ScheduleWork(ToggleValveState);
        }
        else if (AppEvent::kAppEventButtonType_LongPressed == aEvent->ButtonEvent.Type)
        {
            DeviceLayer::PlatformMgr().ScheduleWork(TogglePumpState);
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

void AppTask::ChangeConfigutation(intptr_t arg)
{
    // Change a F attribute to simulate a change in configuration of the device
    uint8_t valveLevelStep = 0;
    Protocols::InteractionModel::Status status =
        Clusters::ValveConfigurationAndControl::Attributes::LevelStep::Get(EndpointId(sWaterValveEndpoint), &valveLevelStep);
    VerifyOrDie(status == Protocols::InteractionModel::Status::Success);

    if (valveLevelStep == 1)
    {
        // Change fixed LevelStep value to 10
        ChipLogProgress(NotSpecified, "Set Valve LevelStep to 10");
        valveLevelStep = 10;
    }
    else
    {
        // Change fixed LevelStep value back to 1
        ChipLogProgress(NotSpecified, "Set Valve LevelStep to 1");
        valveLevelStep = 1;
    }

    status = Clusters::ValveConfigurationAndControl::Attributes::LevelStep::Set(EndpointId(sWaterValveEndpoint), valveLevelStep);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "Failed to set LevelStep value");
    }
    else
    {
        // LevelStep in ValveConfigurationAndControl has been modified,so bump ConfigurationVersion
        // by calling the getter function to obtain a ScopedConfigurationVersionUpdater
        ChipLogProgress(NotSpecified, "Bump ConfigurationVersion");
        DataModel::ProviderMetadataTree::ScopedConfigurationVersionUpdater configurationVersionTransaction =
            InteractionModelEngine::GetInstance()->GetDataModelProvider()->GetNodeDataModelConfigurationVersionUpdater();
    }
}

void AppTask::ToggleValveState(intptr_t arg)
{
    DataModel::Nullable<Percent> level;
    DataModel::Nullable<uint32_t> duration = DataModel::Nullable<uint32_t>(10);

    DataModel::Nullable<Clusters::ValveConfigurationAndControl::ValveStateEnum> attributeValue;
    Clusters::ValveConfigurationAndControl::Attributes::CurrentState::Get(EndpointId(sWaterValveEndpoint), attributeValue);

    if (attributeValue.IsNull())
    {
        ChipLogProgress(NotSpecified, "Toggle valve state: Unknown -> Open");

        level.SetNonNull(Percent(100));
        Clusters::ValveConfigurationAndControl::SetValveLevel(EndpointId(sWaterValveEndpoint), level, duration);
    }
    else if (attributeValue.Value() == Clusters::ValveConfigurationAndControl::ValveStateEnum::kClosed)
    {
        ChipLogProgress(NotSpecified, "Toggle valve state: Closed -> Open");

        level.SetNonNull(Percent(100));
        Clusters::ValveConfigurationAndControl::SetValveLevel(EndpointId(sWaterValveEndpoint), level, duration);
    }
    else
    {
        ChipLogProgress(NotSpecified, "Toggle valve state: Open -> Closed");
        Clusters::ValveConfigurationAndControl::CloseValve(EndpointId(sWaterValveEndpoint));
    }
}

void AppTask::TogglePumpState(intptr_t arg)
{
    bool onOffState;
    Clusters::OnOff::Attributes::OnOff::Get(EndpointId(sPumpEndpoint), &onOffState);

    BitMask<Clusters::PumpConfigurationAndControl::PumpStatusBitmap> pumpStatus;
    Clusters::PumpConfigurationAndControl::Attributes::PumpStatus::Get(EndpointId(sPumpEndpoint), &pumpStatus);

    if (!onOffState)
    {
        ChipLogProgress(NotSpecified, "Toggle pump state: Off -> On");
        pumpStatus.Set(Clusters::PumpConfigurationAndControl::PumpStatusBitmap::kRunning);
    }
    else
    {
        ChipLogProgress(NotSpecified, "Toggle pump state: On -> Off");
        pumpStatus.Clear(Clusters::PumpConfigurationAndControl::PumpStatusBitmap::kRunning);
    }

    Clusters::OnOff::Attributes::OnOff::Set(EndpointId(sPumpEndpoint), !onOffState);
    Clusters::PumpConfigurationAndControl::Attributes::PumpStatus::Set(EndpointId(sPumpEndpoint), pumpStatus);
}

void AppTask::TakeSoilMeasurement(intptr_t arg)
{
    int rd_num = rand() % (100 - 0 + 1) + 0;
    DataModel::Nullable<Percent> fakeMeasurement;
    fakeMeasurement.SetNonNull(Percent(rd_num));

    ChipLogProgress(NotSpecified, "Adjusting soil measurement value: %d", fakeMeasurement.Value());
    gSoilMeasurementServer.Cluster().SetSoilMoistureMeasuredValue(fakeMeasurement);
}

void AppTask::OpenValve(void)
{
    ChipLogProgress(NotSpecified, "Open valve");
    LED_setOn(sAppRedHandle, LED_BRIGHTNESS_MAX);
}

void AppTask::CloseValve(void)
{
    ChipLogProgress(NotSpecified, "Close valve");
    LED_setOff(sAppRedHandle);
}

void AppTask::TurnOnPump(void)
{
    ChipLogProgress(NotSpecified, "Turn on pump");
    LED_setOn(sAppGreenHandle, LED_BRIGHTNESS_MAX);
}

void AppTask::TurnOffPump(void)
{
    ChipLogProgress(NotSpecified, "Turn off pump");
    LED_setOff(sAppGreenHandle);
}

void AppTask::InitOnOff()
{
    Protocols::InteractionModel::Status status;

    ChipLogProgress(NotSpecified, "Init On/Off");

    // Write false as pump always boots in stopped mode
    status = Clusters::OnOff::Attributes::OnOff::Set(EndpointId(sPumpEndpoint), false);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Init On/Off state  %x", to_underlying(status));
    }
}

void AppTask::InitPumpConfigurationAndControl()
{
    Protocols::InteractionModel::Status status;

    ChipLogProgress(NotSpecified, "Init PumpConfigurationAndControl");

    // Write false as pump always boots in stopped mode
    BitMask<Clusters::PumpConfigurationAndControl::PumpStatusBitmap> pumpStatus;
    Clusters::PumpConfigurationAndControl::Attributes::PumpStatus::Get(EndpointId(sPumpEndpoint), &pumpStatus);
    pumpStatus.Clear(Clusters::PumpConfigurationAndControl::PumpStatusBitmap::kRunning);
    status = Clusters::PumpConfigurationAndControl::Attributes::PumpStatus::Set(EndpointId(sPumpEndpoint), pumpStatus);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Pumpstatus error  %x", to_underlying(status));
    }

    // Set operation mode to ConstantSpeed
    status = Clusters::PumpConfigurationAndControl::Attributes::OperationMode::Set(
        EndpointId(sPumpEndpoint), Clusters::PumpConfigurationAndControl::OperationModeEnum::kNormal);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: OperationMode error  %x", to_underlying(status));
    }

    // set effective control mode to ConstantSpeed
    status = Clusters::PumpConfigurationAndControl::Attributes::EffectiveControlMode::Set(
        EndpointId(sPumpEndpoint), Clusters::PumpConfigurationAndControl::ControlModeEnum::kConstantSpeed);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: EffectiveControlMode error  %x", to_underlying(status));
    }

    // set effective operation mode to Normal
    status = Clusters::PumpConfigurationAndControl::Attributes::EffectiveOperationMode::Set(
        EndpointId(sPumpEndpoint), Clusters::PumpConfigurationAndControl::OperationModeEnum::kNormal);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: EffectiveOperationMode error  %x", to_underlying(status));
    }

    // 2000.0 kPa as MaxPressure
    int16_t maxPressure = 20000;
    status = Clusters::PumpConfigurationAndControl::Attributes::MaxPressure::Set(EndpointId(sPumpEndpoint), maxPressure);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MaxPressure  %x", to_underlying(status));
    }

    // 2000 RPM as MaxSpeed
    uint16_t maxSpeed = 2000;
    status            = Clusters::PumpConfigurationAndControl::Attributes::MaxSpeed::Set(EndpointId(sPumpEndpoint), maxSpeed);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MaxSpeed  %x", to_underlying(status));
    }

    // 200.0 m3/h as MaxFlow
    uint16_t maxFlow = 2000;
    status           = Clusters::PumpConfigurationAndControl::Attributes::MaxFlow::Set(EndpointId(sPumpEndpoint), maxFlow);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MaxFlow  %x", to_underlying(status));
    }

    // 200 RPM as MinConstSpeed
    uint16_t minConstSpeed = 200;
    status = Clusters::PumpConfigurationAndControl::Attributes::MinConstSpeed::Set(EndpointId(sPumpEndpoint), minConstSpeed);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MinConstSpeed  %x", to_underlying(status));
    }

    // 2000 RPM as MaxConstSpeed
    uint16_t maxConstSpeed = 2000;
    status = Clusters::PumpConfigurationAndControl::Attributes::MaxConstSpeed::Set(EndpointId(sPumpEndpoint), maxConstSpeed);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MaxConstSpeed  %x", to_underlying(status));
    }
}
