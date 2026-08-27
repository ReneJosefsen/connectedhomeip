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

#define CHIP_DEVICE_CONFIG_ENABLE_BOOLEAN_STATE_CONFIGURATION_TRIGGER 1

#if CHIP_DEVICE_CONFIG_ENABLE_BOOLEAN_STATE_CONFIGURATION_TRIGGER
#include <app/TestEventTriggerDelegate.h>
#include <app/clusters/boolean-state-configuration-server/BooleanStateConfigurationTestEventTriggerHandler.h>
#endif

#include "ValveControlDelegate.h"
#include "thermostat-delegate-impl.h"
#include <app/clusters/basic-information/CodegenIntegration.h>
#include <app/clusters/boolean-state-configuration-server/CodegenIntegration.h>
#include <app/clusters/boolean-state-server/CodegenIntegration.h>
#include <app/clusters/occupancy-sensor-server/OccupancySensingCluster.h>
#include <app/clusters/smoke-co-alarm-server/smoke-co-alarm-server.h>
#include <app/clusters/soil-measurement-server/SoilMeasurementCluster.h>
#include <app/clusters/thermostat-server/CodegenIntegration.h>
#include <app/clusters/thermostat-server/ThermostatCluster.h>
#include <app/clusters/valve-configuration-and-control-server/valve-configuration-and-control-server.h>

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

static Clusters::ValveConfigurationAndControl::ValveControlDelegate sValveDelegate;

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

static std::array<Clusters::SmokeCoAlarm::ExpressedStateEnum, Clusters::SmokeCoAlarmServer::kPriorityOrderLength> sPriorityOrder = {
    Clusters::SmokeCoAlarm::ExpressedStateEnum::kInoperative,       Clusters::SmokeCoAlarm::ExpressedStateEnum::kSmokeAlarm,
    Clusters::SmokeCoAlarm::ExpressedStateEnum::kInterconnectSmoke, Clusters::SmokeCoAlarm::ExpressedStateEnum::kCOAlarm,
    Clusters::SmokeCoAlarm::ExpressedStateEnum::kInterconnectCO,    Clusters::SmokeCoAlarm::ExpressedStateEnum::kHardwareFault,
    Clusters::SmokeCoAlarm::ExpressedStateEnum::kTesting,           Clusters::SmokeCoAlarm::ExpressedStateEnum::kEndOfService,
    Clusters::SmokeCoAlarm::ExpressedStateEnum::kBatteryAlert
};

static LazyRegisteredServerCluster<Clusters::OccupancySensingCluster> gOccupancySensingServer;

static uint8_t sCurrentEndpoint   = 0;
static const uint8_t sMaxEndpoint = sWaterLeakDetectorEndpointId;

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

    sRequestorStorage.Init(Server::GetInstance().GetPersistentStorage());
    sRequestorCore.Init(Server::GetInstance(), sRequestorStorage, sRequestorUser, sDownloader);
    sImageProcessor.SetOTADownloader(&sDownloader);
    sDownloader.SetImageProcessorDelegate(&sImageProcessor);
    sRequestorUser.Init(&sRequestorCore, &sImageProcessor);
}
#endif

void AppTask::SoilMeasurementTimerEventHandler(TimerHandle_t xTimer)
{
    TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(TakeSoilMeasurement);
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

void AppTask::InitOccupancySensing(EndpointId endpointId)
{
    ChipLogProgress(AppServer, "OccupancySensing cluster init - %d", endpointId);

    Clusters::OccupancySensingCluster::Config config(endpointId);
    config.WithFeatures(
        BitFlags(Clusters::OccupancySensing::Feature::kPassiveInfrared, Clusters::OccupancySensing::Feature::kOccupancyEvent));

    gOccupancySensingServer.Create(config);

    CHIP_ERROR err = CodegenDataModelProvider::Instance().Registry().Register(gOccupancySensingServer.Registration());
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "OccupancySensing cluster error registration %" CHIP_ERROR_FORMAT, err.Format());
    }
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

    TEMPORARY_RETURN_IGNORED sThreadNetworkDriver.Init();
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
    TEMPORARY_RETURN_IGNORED GetCommissionableDataProvider() -> GetSetupDiscriminator(deviceDiscriminator);
    char deviceName[GAP_DEVICE_NAME_LEN + 1] = { 0 };
    snprintf(deviceName, GAP_DEVICE_NAME_LEN, "%s%04u", CHIP_DEVICE_CONFIG_BLE_DEVICE_NAME_PREFIX, deviceDiscriminator);
    TEMPORARY_RETURN_IGNORED ConnectivityMgr().SetBLEDeviceName(deviceName);
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

    // TestEventTrigger
#if CHIP_DEVICE_CONFIG_ENABLE_BOOLEAN_STATE_CONFIGURATION_TRIGGER
    static SimpleTestEventTriggerDelegate sTestEventTriggerDelegate{};
    static BooleanStateConfigurationTestEventTriggerHandler sBooleanStateConfigurationTestEventTriggerHandler{};
    VerifyOrDie(sTestEventTriggerDelegate.Init(ByteSpan(sTestEventTriggerEnableKey)) == CHIP_NO_ERROR);
    VerifyOrDie(sTestEventTriggerDelegate.AddHandler(&sBooleanStateConfigurationTestEventTriggerHandler) == CHIP_NO_ERROR);
    initParams.testEventTriggerDelegate = &sTestEventTriggerDelegate;
#endif

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

    TEMPORARY_RETURN_IGNORED Server::GetInstance().Init(initParams);

    // Init Occupancy Sensing cluster
    sAppTask.InitOccupancySensing(sOccupancySensorEndpointId);

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
    Clusters::ValveConfigurationAndControl::SetDefaultDelegate(EndpointId(sWaterValveEndpointId), &sValveDelegate);

    // Set Thermostat delegate
    auto & delegate = Clusters::Thermostat::ThermostatDelegate::GetInstance();
    Clusters::Thermostat::SetDefaultDelegate(EndpointId(sThermostatEndpointId), &delegate);

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
                TEMPORARY_RETURN_IGNORED ConnectivityMgr().SetBLEAdvertisingEnabled(false);
                ChipLogProgress(NotSpecified, "Disabled BLE Advertisements");
            }
        }
        else if (AppEvent::kAppEventButtonType_DoubleClicked == aEvent->ButtonEvent.Type)
        {
            TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(ChangeConfigutation);
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
            if (sCurrentEndpoint == sRootNodeEndpointId)
            {
                ChipLogProgress(NotSpecified, "No action on Root Node");
            }
            else if (sCurrentEndpoint == sWaterValveEndpointId)
            {
                TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(ToggleValveState);
            }
            else if (sCurrentEndpoint == sPumpEndpointId)
            {
                TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(TogglePumpState);
            }
            else if (sCurrentEndpoint == sSoilSensorEndpointId)
            {
                TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(TakeSoilMeasurement);
            }
            else if (sCurrentEndpoint == sThermostatEndpointId)
            {
                ChipLogProgress(NotSpecified, "No action on Thermostat");
            }
            else if (sCurrentEndpoint == sSmokeCoEndpointId)
            {
                TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(ToggleSmokeCoState);
            }
            else if (sCurrentEndpoint == sOccupancySensorEndpointId)
            {
                TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(ToggleOccupancySensorState);
            }
            else if (sCurrentEndpoint == sWaterLeakDetectorEndpointId)
            {
                TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(ToggleWaterLeakDetectorState);
            }
        }
        else if (AppEvent::kAppEventButtonType_DoubleClicked == aEvent->ButtonEvent.Type)
        {
            // Change the endpoint id to act on for single press
            if (sCurrentEndpoint < sMaxEndpoint)
            {
                sCurrentEndpoint++;
            }
            else
            {
                sCurrentEndpoint = 0;
            }

            ChipLogProgress(NotSpecified, "Current endpoint is %d", sCurrentEndpoint);

            if (sCurrentEndpoint == sRootNodeEndpointId)
            {
                ChipLogProgress(NotSpecified, "Selected Device Type: Root Node");
            }
            else if (sCurrentEndpoint == sWaterValveEndpointId)
            {
                ChipLogProgress(NotSpecified, "Selected Device Type: Water Valve");
            }
            else if (sCurrentEndpoint == sPumpEndpointId)
            {
                ChipLogProgress(NotSpecified, "Selected Device Type: Pump");
            }
            else if (sCurrentEndpoint == sSoilSensorEndpointId)
            {
                ChipLogProgress(NotSpecified, "Selected Device Type: Soil Sensor");
            }
            else if (sCurrentEndpoint == sThermostatEndpointId)
            {
                ChipLogProgress(NotSpecified, "Selected Device Type: Thermostat");
            }
            else if (sCurrentEndpoint == sSmokeCoEndpointId)
            {
                ChipLogProgress(NotSpecified, "Selected Device Type: Smoke CO Alarm");
            }
            else if (sCurrentEndpoint == sOccupancySensorEndpointId)
            {
                ChipLogProgress(NotSpecified, "Selected Device Type: Occupancy Sensor");
            }
            else if (sCurrentEndpoint == sWaterLeakDetectorEndpointId)
            {
                ChipLogProgress(NotSpecified, "Selected Device Type: Water Leak Detector");
            }
        }
        else if (AppEvent::kAppEventButtonType_LongPressed == aEvent->ButtonEvent.Type)
        {
            if (sCurrentEndpoint == sWaterLeakDetectorEndpointId)
            {
                TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(ToggleWaterLeakSensorFault);
            }
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
    DataModel::Nullable<uint16_t> pumpMaxSpeed = DataModel::Nullable<uint16_t>();
    Protocols::InteractionModel::Status status =
        Clusters::PumpConfigurationAndControl::Attributes::MaxSpeed::Get(EndpointId(sPumpEndpointId), pumpMaxSpeed);
    VerifyOrDie(status == Protocols::InteractionModel::Status::Success);

    if (pumpMaxSpeed.IsNull())
    {
        // Change fixed MaxSpeed value to 2000
        ChipLogProgress(NotSpecified, "Initially set Pump MaxSpeed to 2000");
        pumpMaxSpeed.SetNonNull(2000);
    }
    if (pumpMaxSpeed.Value() == 1000)
    {
        // Change fixed MaxSpeed value to 2000
        ChipLogProgress(NotSpecified, "Set Pump MaxSpeed to 2000");
        pumpMaxSpeed.SetNonNull(2000);
    }
    else
    {
        // Change fixed MaxSpeed value back to 1000
        ChipLogProgress(NotSpecified, "Set Pump MaxSpeed to 1000");
        pumpMaxSpeed.SetNonNull(1000);
    }

    status = Clusters::PumpConfigurationAndControl::Attributes::MaxSpeed::Set(EndpointId(sPumpEndpointId), pumpMaxSpeed);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "Failed to set MaxSpeed value");
    }
    else
    {
        // MaxSpeed in PumpConfigurationAndControl has been modified,so bump ConfigurationVersion
        Clusters::BasicInformationCluster * cluster = Clusters::BasicInformation::GetClusterInstance();
        if (cluster == nullptr)
        {
            ChipLogError(NotSpecified, "No basic information cluster available. Invalid state.");
        }
        else
        {
            LogErrorOnFailure(cluster->IncreaseConfigurationVersion());
        }
    }
}

void AppTask::ToggleValveState(intptr_t arg)
{
    DataModel::Nullable<Percent> level;
    DataModel::Nullable<uint32_t> duration = DataModel::Nullable<uint32_t>(10);

    Clusters::ValveConfigurationAndControlCluster * valveCluster =
        Clusters::ValveConfigurationAndControl::FindClusterOnEndpoint(sWaterValveEndpointId);

    DataModel::Nullable<Clusters::ValveConfigurationAndControl::ValveStateEnum> attributeValue = valveCluster->GetCurrentState();

    if (attributeValue.IsNull())
    {
        ChipLogProgress(NotSpecified, "Toggle valve state: Unknown -> Open");

        level.SetNonNull(Percent(100));
        TEMPORARY_RETURN_IGNORED valveCluster->OpenValve(level, duration);
    }
    else if (attributeValue.Value() == Clusters::ValveConfigurationAndControl::ValveStateEnum::kClosed)
    {
        ChipLogProgress(NotSpecified, "Toggle valve state: Closed -> Open");

        level.SetNonNull(Percent(100));
        TEMPORARY_RETURN_IGNORED valveCluster->OpenValve(level, duration);
    }
    else
    {
        ChipLogProgress(NotSpecified, "Toggle valve state: Open -> Closed");
        TEMPORARY_RETURN_IGNORED valveCluster->CloseValve();
    }
}

void AppTask::TogglePumpState(intptr_t arg)
{
    bool onOffState;
    Clusters::OnOff::Attributes::OnOff::Get(EndpointId(sPumpEndpointId), &onOffState);

    BitMask<Clusters::PumpConfigurationAndControl::PumpStatusBitmap> pumpStatus;
    Clusters::PumpConfigurationAndControl::Attributes::PumpStatus::Get(EndpointId(sPumpEndpointId), &pumpStatus);

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

    Clusters::OnOff::Attributes::OnOff::Set(EndpointId(sPumpEndpointId), !onOffState);
    Clusters::PumpConfigurationAndControl::Attributes::PumpStatus::Set(EndpointId(sPumpEndpointId), pumpStatus);
}

void AppTask::TakeSoilMeasurement(intptr_t arg)
{
    int rd_num = rand() % (100 - 0 + 1) + 0;
    DataModel::Nullable<Percent> fakeMeasurement;
    fakeMeasurement.SetNonNull(Percent(rd_num));

    ChipLogProgress(NotSpecified, "Adjusting soil measurement value: %d", fakeMeasurement.Value());
    TEMPORARY_RETURN_IGNORED gSoilMeasurementServer.Cluster().SetSoilMoistureMeasuredValue(fakeMeasurement);
}

void AppTask::ToggleSmokeCoState(intptr_t arg)
{
    auto & smokeCoServer = Clusters::SmokeCoAlarmServer::Instance();

    bool currentUnmountedState;
    smokeCoServer.GetUnmountedState(sSmokeCoEndpointId, currentUnmountedState);

    if (currentUnmountedState)
    {
        ChipLogProgress(NotSpecified, "Mounting SmokeCO Alarm");
    }
    else
    {
        ChipLogProgress(NotSpecified, "Unmounting SmokeCO Alarm");
    }

    smokeCoServer.SetUnmountedState(sSmokeCoEndpointId, !currentUnmountedState);
    smokeCoServer.SetExpressedStateByPriority(sSmokeCoEndpointId, sPriorityOrder);
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
    status = Clusters::OnOff::Attributes::OnOff::Set(EndpointId(sPumpEndpointId), false);
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
    Clusters::PumpConfigurationAndControl::Attributes::PumpStatus::Get(EndpointId(sPumpEndpointId), &pumpStatus);
    pumpStatus.Clear(Clusters::PumpConfigurationAndControl::PumpStatusBitmap::kRunning);
    status = Clusters::PumpConfigurationAndControl::Attributes::PumpStatus::Set(EndpointId(sPumpEndpointId), pumpStatus);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Pumpstatus error  %x", to_underlying(status));
    }

    // Set operation mode to ConstantSpeed
    status = Clusters::PumpConfigurationAndControl::Attributes::OperationMode::Set(
        EndpointId(sPumpEndpointId), Clusters::PumpConfigurationAndControl::OperationModeEnum::kNormal);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: OperationMode error  %x", to_underlying(status));
    }

    // set effective control mode to ConstantSpeed
    status = Clusters::PumpConfigurationAndControl::Attributes::EffectiveControlMode::Set(
        EndpointId(sPumpEndpointId), Clusters::PumpConfigurationAndControl::ControlModeEnum::kConstantSpeed);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: EffectiveControlMode error  %x", to_underlying(status));
    }

    // set effective operation mode to Normal
    status = Clusters::PumpConfigurationAndControl::Attributes::EffectiveOperationMode::Set(
        EndpointId(sPumpEndpointId), Clusters::PumpConfigurationAndControl::OperationModeEnum::kNormal);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: EffectiveOperationMode error  %x", to_underlying(status));
    }

    // 2000.0 kPa as MaxPressure
    int16_t maxPressure = 20000;
    status = Clusters::PumpConfigurationAndControl::Attributes::MaxPressure::Set(EndpointId(sPumpEndpointId), maxPressure);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MaxPressure  %x", to_underlying(status));
    }

    // 2000 RPM as MaxSpeed
    uint16_t maxSpeed = 2000;
    status            = Clusters::PumpConfigurationAndControl::Attributes::MaxSpeed::Set(EndpointId(sPumpEndpointId), maxSpeed);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MaxSpeed  %x", to_underlying(status));
    }

    // 200.0 m3/h as MaxFlow
    uint16_t maxFlow = 2000;
    status           = Clusters::PumpConfigurationAndControl::Attributes::MaxFlow::Set(EndpointId(sPumpEndpointId), maxFlow);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MaxFlow  %x", to_underlying(status));
    }

    // 200 RPM as MinConstSpeed
    uint16_t minConstSpeed = 200;
    status = Clusters::PumpConfigurationAndControl::Attributes::MinConstSpeed::Set(EndpointId(sPumpEndpointId), minConstSpeed);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MinConstSpeed  %x", to_underlying(status));
    }

    // 2000 RPM as MaxConstSpeed
    uint16_t maxConstSpeed = 2000;
    status = Clusters::PumpConfigurationAndControl::Attributes::MaxConstSpeed::Set(EndpointId(sPumpEndpointId), maxConstSpeed);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(NotSpecified, "ERR: Updating MaxConstSpeed  %x", to_underlying(status));
    }
}

void AppTask::InitSmokeCoAlarm()
{
    Clusters::SmokeCoAlarmCluster::Config config;
    config.featureMap.Set(Clusters::SmokeCoAlarm::Feature::kSmokeAlarm);
    config.featureMap.Set(Clusters::SmokeCoAlarm::Feature::kCoAlarm);

    config.optionalAttribs =
        Clusters::SmokeCoAlarmCluster::OptionalAttributeSet(Clusters::SmokeCoAlarmCluster::OptionalAttributeSet::All());

    CHIP_ERROR ret = Clusters::SmokeCoAlarmServer::Instance().Init(sSmokeCoEndpointId, config);
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "SmokeCoAlarmServer::Init() failed: %" CHIP_ERROR_FORMAT, ret.Format());
        return;
    }

    Clusters::SmokeCoAlarmServer::Instance().SetInoperativeWhenUnmounted(true);
}

void AppTask::ToggleOccupancySensorState(intptr_t arg)
{
    bool attributeValue = gOccupancySensingServer.Cluster().IsOccupied();
    ChipLogProgress(NotSpecified, "Toggle OccupancySensor state: %d -> %d", attributeValue, !attributeValue);
    gOccupancySensingServer.Cluster().SetOccupancy(!attributeValue);
}

void AppTask::ToggleWaterLeakDetectorState(intptr_t arg)
{
    auto booleanState = Clusters::BooleanState::FindClusterOnEndpoint(sWaterLeakDetectorEndpointId);
    VerifyOrReturn(booleanState != nullptr);
    bool attributeValue = booleanState->GetStateValue();
    ChipLogProgress(NotSpecified, "Toggle WaterLeakDetector state: %d -> %d", attributeValue, !attributeValue);

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
    auto booleanStateConfigCluster = Clusters::BooleanStateConfiguration::FindClusterOnEndpoint(sWaterLeakDetectorEndpointId);
    BitMask<Clusters::BooleanStateConfiguration::AlarmModeBitmap> alarmsEnabled = booleanStateConfigCluster->GetAlarmsEnabled();

    if (alarmsEnabled.Has(Clusters::BooleanStateConfiguration::AlarmModeBitmap::kVisual))
    {
        LED_setOn(sAppRedHandle, LED_BRIGHTNESS_MAX);
    }
    else
    {
        ChipLogProgress(NotSpecified, "Visual alarming is disabled, LED not turned on");
    }

    auto booleanStateCluster = Clusters::BooleanState::FindClusterOnEndpoint(sWaterLeakDetectorEndpointId);
    VerifyOrReturn(booleanStateCluster != nullptr);
    booleanStateCluster->SetStateValue(true);
    TEMPORARY_RETURN_IGNORED Clusters::BooleanStateConfiguration::SetAllEnabledAlarmsActive(sWaterLeakDetectorEndpointId);
}

void AppTask::LeakDetectorUntrigger(void)
{
    LED_setOff(sAppRedHandle);

    auto booleanStateCluster = Clusters::BooleanState::FindClusterOnEndpoint(sWaterLeakDetectorEndpointId);
    VerifyOrReturn(booleanStateCluster != nullptr);
    booleanStateCluster->SetStateValue(false);
    TEMPORARY_RETURN_IGNORED Clusters::BooleanStateConfiguration::ClearAllAlarms(sWaterLeakDetectorEndpointId);
}

void AppTask::ToggleWaterLeakSensorFault(intptr_t arg)
{
    auto booleanStateConfigCluster = Clusters::BooleanStateConfiguration::FindClusterOnEndpoint(sWaterLeakDetectorEndpointId);
    VerifyOrReturn(booleanStateConfigCluster != nullptr);
    BitMask<Clusters::BooleanStateConfiguration::SensorFaultBitmap> sensorFaults = booleanStateConfigCluster->GetSensorFault();
    if (sensorFaults.Has(Clusters::BooleanStateConfiguration::SensorFaultBitmap::kGeneralFault))
    {
        ChipLogProgress(NotSpecified, "Clearing sensor fault");
        sensorFaults.Clear(Clusters::BooleanStateConfiguration::SensorFaultBitmap::kGeneralFault);
        booleanStateConfigCluster->GenerateSensorFault(sensorFaults);
    }
    else
    {
        ChipLogProgress(NotSpecified, "Setting sensor fault");
        sensorFaults.Set(Clusters::BooleanStateConfiguration::SensorFaultBitmap::kGeneralFault);
        booleanStateConfigCluster->GenerateSensorFault(sensorFaults);
    }
}
