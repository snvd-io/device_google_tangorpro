/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.power.stats-service.pixel"

#include <dataproviders/DisplayStateResidencyDataProvider.h>
#include <dataproviders/GenericStateResidencyDataProvider.h>
#include <dataproviders/PowerStatsEnergyConsumer.h>
#include <DevfreqStateResidencyDataProvider.h>
#include <Gs201CommonDataProviders.h>
#include <PowerStatsAidl.h>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

using aidl::android::hardware::power::stats::DevfreqStateResidencyDataProvider;
using aidl::android::hardware::power::stats::DisplayStateResidencyDataProvider;
using aidl::android::hardware::power::stats::EnergyConsumerType;
using aidl::android::hardware::power::stats::GenericStateResidencyDataProvider;
using aidl::android::hardware::power::stats::PowerStatsEnergyConsumer;

void addDisplay(std::shared_ptr<PowerStats> p) {
    // Add display residency stats
    std::vector<std::string> states = {
        "Off",
        "On: 1600x2560@60",
        "HBM: 1600x2560@60"};

    p->addStateResidencyDataProvider(std::make_unique<DisplayStateResidencyDataProvider>("Display",
            "/sys/class/backlight/panel0-backlight/state",
            states));

    // Add display energy consumer
    p->addEnergyConsumer(PowerStatsEnergyConsumer::createMeterConsumer(
            p,
            EnergyConsumerType::DISPLAY,
            "Display",
            {"VSYS_PWR_DISPLAY"}));
}

void addGPUGs202(std::shared_ptr<PowerStats> p) {
    std::map<std::string, int32_t> stateCoeffs;

    // Add GPU state residency
    p->addStateResidencyDataProvider(std::make_unique<DevfreqStateResidencyDataProvider>(
            "GPU",
            "/sys/devices/platform/28000000.mali"));

    // Add GPU energy consumer
    stateCoeffs = {
        {"202000",  890},
        {"251000", 1102},
        {"302000", 1308},
        {"351000", 1522},
        {"400000", 1772},
        {"434000", 1931},
        {"471000", 2105},
        {"510000", 2292},
        {"572000", 2528},
        {"633000", 2811},
        {"701000", 3127},
        {"762000", 3452},
        {"848000", 4044}};

    p->addEnergyConsumer(PowerStatsEnergyConsumer::createMeterAndAttrConsumer(
            p,
            EnergyConsumerType::OTHER,
            "GPU",
            {"S2S_VDD_G3D", "S8S_VDD_G3D_L2"},
            {{UID_TIME_IN_STATE, "/sys/devices/platform/28000000.mali/uid_time_in_state"}},
            stateCoeffs));
}

void addUwb(std::shared_ptr<PowerStats> p) {
    // A constant to represent the number of nanoseconds in one millisecond.
    const int NS_TO_MS = 1000000;

    // ACPM stats are reported in nanoseconds. The transform function
    // converts nanoseconds to milliseconds.
    std::function<uint64_t(uint64_t)> uwbNsToMs = [](uint64_t a) { return a / NS_TO_MS; };
    const GenericStateResidencyDataProvider::StateResidencyConfig stateConfig = {
            .entryCountSupported = true,
            .entryCountPrefix = "count:",
            .totalTimeSupported = true,
            .totalTimePrefix = "dur ns:",
            .totalTimeTransform = uwbNsToMs,
            .lastEntrySupported = false,
    };

    const std::vector<std::pair<std::string, std::string>> stateHeaders = {
            std::make_pair("Off", "Off state:"),
            std::make_pair("Deep sleep", "Deep sleep state:"),
            std::make_pair("Run", "Run state:"),
            std::make_pair("Idle", "Idle state:"),
            std::make_pair("Tx", "Tx state:"),
            std::make_pair("Rx", "Rx state:"),
    };

    std::vector<GenericStateResidencyDataProvider::PowerEntityConfig> cfgs;
    cfgs.emplace_back(generateGenericStateResidencyConfigs(stateConfig, stateHeaders),
            "UWB", "");

    p->addStateResidencyDataProvider(std::make_unique<GenericStateResidencyDataProvider>(
            "/sys/devices/platform/10db0000.spi/spi_master/spi16/spi16.0/uwb/power_stats", cfgs));
}

int main() {
    LOG(INFO) << "Pixel PowerStats HAL AIDL Service is starting.";

    // single thread
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<PowerStats> p = ndk::SharedRefBase::make<PowerStats>();

    setEnergyMeter(p);
    addAoC(p);
    addPixelStateResidencyDataProvider(p);
    addCPUclusters(p);
    addDisplay(p);
    addSoC(p);
    addWifi(p);
    addTPU(p);
    addUfs(p);
    addUwb(p);
    addPowerDomains(p);
    addDevfreq(p);
    addGPUGs202(p);
    addDvfsStats(p);

    const std::string instance = std::string() + PowerStats::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(p->asBinder().get(), instance.c_str());
    LOG_ALWAYS_FATAL_IF(status != STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
