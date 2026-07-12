#define LOG_MODULE_PRX      "Inst.Sensor.Layer"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

#include "SDK/Simulator/Components/InstanceSensorLayer.hpp"
#include "../../simulator/ConfigurationSimulator.hpp"
#include "SDK/Simulator/Components/SensorManager.hpp"
#include "SDK/Simulator/Components/ComponentSimulator.hpp"
#include <SDK/Simulator/Kernel/Mock/System.hpp>

// Defaults for apps whose ConfigurationSimulator.hpp predates the IMU fusion
// sensor: enabling it is a per-app opt-in, the other knobs get sane values.
#if !defined(IMU_FUSION_SIM_SWING_KEY)
#define IMU_FUSION_SIM_SWING_KEY '6'
#endif
#if !defined(IMU_FUSION_SIM_CSV_PATH)
#define IMU_FUSION_SIM_CSV_PATH ""
#endif

namespace Instance {

    SensorLayer& SensorLayer::getInstance()
    {
        static SensorLayer mInstance;

        return mInstance;
    }

    void SensorLayer::init()
    {
        Sensor::Manager& sm = Sensor::Manager::getInstance();

#if defined(GSP_SIM_ENABLE) && (GSP_SIM_ENABLE == 1)
        sm.regDriver(&mSensorGpsAltimer.getDriver());
        sm.regDriver(&mSensorGpsLocation.getDriver());
        sm.regDriver(&mSensorGpsSpeed.getDriver());
        sm.regDriver(&mSensorGpsDistance.getDriver());
#endif // GSP_SIM_ENABLE = 1

#if defined(HEAT_RATE_SIM_ENABLE) && (HEAT_RATE_SIM_ENABLE == 1)
        sm.regDriver(&mSensorHRMetrics.getDriver());
        sm.regDriver(&mSensorHR.getDriver());
#endif //HEAT_RATE_SIM_ENABLE

#if defined(PRESSURE_SIM_ENABLE) && (PRESSURE_SIM_ENABLE == 1)

#endif //PRESSURE_SIM_ENABLE
        sm.regDriver(&mSensorPressure.getDriver());
#if defined(BATT_LEVEL_SIM_ENABLE) && (BATT_LEVEL_SIM_ENABLE == 1)
        sm.regDriver(&mSensorBatteryLevel.getDriver());
#endif //BATT_LEVEL_SIM_ENABLE

#if defined(IMU_WRIST_SIM_ENABLE) && (IMU_WRIST_SIM_ENABLE == 1)
        LOG_INFO("\n"
            "---------------------------------------------------------\n"
            "|   For Simulation Wrist detect use %c Key in keyboard. |\n"
            "---------------------------------------------------------\n", IMU_WRIST_SIM_WRIST_DETECT_KEY);
        sm.regDriver(&mSensorImuWrist.getDriver());
#endif //IMU_SIM_ENABLE

#if defined(IMU_STEP_COUNTER_SIM_ENABLE) && (IMU_STEP_COUNTER_SIM_ENABLE == 1)
        sm.regDriver(&mSensorImuStepCounter.getDriver());
#endif //IMU_STEP_COUNTER_SIM_ENABLE

#if defined(IMU_RUNNING_CADENCE_SIM_ENABLE) && (IMU_RUNNING_CADENCE_SIM_ENABLE == 1)
        sm.regDriver(&mSensorImuRunningCadence.getDriver());
#endif //IMU_RUNNING_CADENCE_SIM_ENABLE

#if defined(IMU_FUSION_SIM_ENABLE) && (IMU_FUSION_SIM_ENABLE == 1)
        LOG_INFO("\n"
            "---------------------------------------------------------\n"
            "|   For a synthetic racquet swing use %c Key in keyboard. |\n"
            "---------------------------------------------------------\n", IMU_FUSION_SIM_SWING_KEY);
        mSensorImuFusion.configure(IMU_FUSION_SIM_CSV_PATH);
        sm.regDriver(&mSensorImuFusion.getDriver());
#endif //IMU_FUSION_SIM_ENABLE

    }

    void SensorLayer::handlerButtons(uint8_t key)
    {
        if (IMU_WRIST_SIM_WRIST_DETECT_KEY == key && IMU_WRIST_SIM_ENABLE == 1) {
            mSensorImuWrist.handleWristMotion(SDK::Simulator::Mock::System::GetTimeMs());
        }

#if defined(IMU_FUSION_SIM_ENABLE) && (IMU_FUSION_SIM_ENABLE == 1)
        if (IMU_FUSION_SIM_SWING_KEY == key) {
            mSensorImuFusion.injectSwing();
        }
#endif //IMU_FUSION_SIM_ENABLE
    }

    SensorLayer::SensorLayer()
        : mSensorGpsAltimer()
        , mSensorGpsLocation()
        , mSensorGpsSpeed()
        , mSensorGpsDistance()
        , mSensorBatteryLevel()
        , mSensorImuWrist()
        , mSensorHR()
        , mSensorHRMetrics()
        , mSensorPressure()
        , mSensorImuStepCounter()
        , mSensorImuRunningCadence()
        , mSensorImuFusion()
    {
        ComponentSimulator& mComponent = ComponentSimulator::GetInstance();

        mComponent.setParamBatterySimulation(BATT_LEVEL_SIM_START_VALUE, BATT_LEVEL_SIM_STEP_VALUE);
        mComponent.setParamGpsSimulation(GSP_SIM_SPEED_MIN, GPS_SIM_SPEED_BASE, GPS_SIM_SPEED_MAX, GPS_SIM_TIME_SEACH_SATELLITE);
        mComponent.setParamHeartRateSimulation(HEAT_RATE_SIM_MIN_HR, HEAT_RATE_SIM_MAX_HR, HEAT_RATE_SIM_TYPE_TRAINING);
        mComponent.setParamPressureSimulation(PRESSURE_SIM_PRESS_VALLUE);
        mComponent.setParamStepCounterSimulation(IMU_STEP_COUNTER_SIM_STRIDE_LENGTH);
    }
    }

