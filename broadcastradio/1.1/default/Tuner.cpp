/*
 * Copyright (C) 2017 The Android Open Source Project
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

#define LOG_TAG "Tuner"
//#define LOG_NDEBUG 0

#include <log/log.h>

#include "BroadcastRadio.h"
#include "Tuner.h"
#include "Utils.h"
#include <system/RadioMetadataWrapper.h>

namespace android {
namespace hardware {
namespace broadcastradio {
namespace V1_1 {
namespace implementation {

void Tuner::onCallback(radio_hal_event_t *halEvent)
{
    BandConfig config;
    ProgramInfo info;
    hidl_vec<MetaData> metadata;

    if (mCallback != 0) {
        switch(halEvent->type) {
        case RADIO_EVENT_CONFIG:
            Utils::convertBandConfigFromHal(&config, &halEvent->config);
            mCallback->configChange(Utils::convertHalResult(halEvent->status), config);
            break;
        case RADIO_EVENT_ANTENNA:
            mCallback->antennaStateChange(halEvent->on);
            break;
        case RADIO_EVENT_TUNED:
            Utils::convertProgramInfoFromHal(&info, &halEvent->info);
            if (mCallback1_1 != nullptr) {
                mCallback1_1->tuneComplete_1_1(Utils::convertHalResult(halEvent->status), info);
            }
            mCallback->tuneComplete(Utils::convertHalResult(halEvent->status), info.base);
            break;
        case RADIO_EVENT_METADATA: {
            uint32_t channel;
            uint32_t sub_channel;
            if (radio_metadata_get_channel(halEvent->metadata, &channel, &sub_channel) == 0) {
                Utils::convertMetaDataFromHal(metadata, halEvent->metadata);
                mCallback->newMetadata(channel, sub_channel, metadata);
            }
            } break;
        case RADIO_EVENT_TA:
            mCallback->trafficAnnouncement(halEvent->on);
            break;
        case RADIO_EVENT_AF_SWITCH:
            Utils::convertProgramInfoFromHal(&info, &halEvent->info);
            if (mCallback1_1 != nullptr) {
                mCallback1_1->afSwitch_1_1(info);
            }
            mCallback->afSwitch(info.base);
            break;
        case RADIO_EVENT_EA:
            mCallback->emergencyAnnouncement(halEvent->on);
            break;
        case RADIO_EVENT_HW_FAILURE:
        default:
            mCallback->hardwareFailure();
            break;
        }
    }
}

//static
void Tuner::callback(radio_hal_event_t *halEvent, void *cookie)
{
    wp<Tuner> weak(reinterpret_cast<Tuner*>(cookie));
    sp<Tuner> tuner = weak.promote();
    if (tuner == 0) return;
    tuner->onCallback(halEvent);
}

Tuner::Tuner(const sp<V1_0::ITunerCallback>& callback, const wp<BroadcastRadio>& parentDevice)
        : mHalTuner(NULL), mCallback(callback), mCallback1_1(ITunerCallback::castFrom(callback)),
        mParentDevice(parentDevice)
{
    ALOGV("%s", __FUNCTION__);
}


Tuner::~Tuner()
{
    ALOGV("%s", __FUNCTION__);
    const sp<BroadcastRadio> parentDevice = mParentDevice.promote();
    if (parentDevice != 0) {
        parentDevice->closeHalTuner(mHalTuner);
    }
}

// Methods from ::android::hardware::broadcastradio::V1_1::ITuner follow.
Return<Result> Tuner::setConfiguration(const BandConfig& config)  {
    ALOGV("%s", __FUNCTION__);
    if (mHalTuner == NULL) {
        return Utils::convertHalResult(-ENODEV);
    }
    radio_hal_band_config_t halConfig;
    Utils::convertBandConfigToHal(&halConfig, &config);
    int rc = mHalTuner->set_configuration(mHalTuner, &halConfig);
    return Utils::convertHalResult(rc);
}

Return<void> Tuner::getConfiguration(getConfiguration_cb _hidl_cb)  {
    int rc;
    radio_hal_band_config_t halConfig;
    BandConfig config;

    ALOGV("%s", __FUNCTION__);
    if (mHalTuner == NULL) {
        rc = -ENODEV;
        goto exit;
    }
    rc = mHalTuner->get_configuration(mHalTuner, &halConfig);
    if (rc == 0) {
        Utils::convertBandConfigFromHal(&config, &halConfig);
    }

exit:
    _hidl_cb(Utils::convertHalResult(rc), config);
    return Void();
}

Return<Result> Tuner::scan(Direction direction, bool skipSubChannel)  {
    if (mHalTuner == NULL) {
        return Utils::convertHalResult(-ENODEV);
    }
    int rc = mHalTuner->scan(mHalTuner, static_cast<radio_direction_t>(direction), skipSubChannel);
    return Utils::convertHalResult(rc);
}

Return<Result> Tuner::step(Direction direction, bool skipSubChannel)  {
    if (mHalTuner == NULL) {
        return Utils::convertHalResult(-ENODEV);
    }
    int rc = mHalTuner->step(mHalTuner, static_cast<radio_direction_t>(direction), skipSubChannel);
    return Utils::convertHalResult(rc);
}

Return<Result> Tuner::tune(uint32_t channel, uint32_t subChannel)  {
    if (mHalTuner == NULL) {
        return Utils::convertHalResult(-ENODEV);
    }
    int rc = mHalTuner->tune(mHalTuner, channel, subChannel);
    return Utils::convertHalResult(rc);
}

Return<Result> Tuner::cancel()  {
    if (mHalTuner == NULL) {
        return Utils::convertHalResult(-ENODEV);
    }
    int rc = mHalTuner->cancel(mHalTuner);
    return Utils::convertHalResult(rc);
}

Return<void> Tuner::getProgramInformation(getProgramInformation_cb _hidl_cb)  {
    ALOGV("%s", __FUNCTION__);
    return getProgramInformation_1_1([&](Result result, const ProgramInfo& info) {
        _hidl_cb(result, info.base);
    });
}

Return<void> Tuner::getProgramInformation_1_1(getProgramInformation_1_1_cb _hidl_cb)  {
    int rc;
    radio_program_info_t halInfo;
    RadioMetadataWrapper metadataWrapper(&halInfo.metadata);
    ProgramInfo info;

    ALOGV("%s", __FUNCTION__);
    if (mHalTuner == NULL) {
        rc = -ENODEV;
        goto exit;
    }

    rc = mHalTuner->get_program_information(mHalTuner, &halInfo);
    if (rc == 0) {
        Utils::convertProgramInfoFromHal(&info, &halInfo);
    }

exit:
    _hidl_cb(Utils::convertHalResult(rc), info);
    return Void();
}

Return<ProgramListResult> Tuner::startBackgroundScan() {
    return ProgramListResult::UNAVAILABLE;
}

Return<void> Tuner::getProgramList(const hidl_string& filter __unused, getProgramList_cb _hidl_cb) {
    hidl_vec<ProgramInfo> pList;
    // TODO(b/34054813): do the actual implementation.
    _hidl_cb(ProgramListResult::NOT_STARTED, pList);
    return Void();
}

Return<void> Tuner::isAnalogForced(isAnalogForced_cb _hidl_cb) {
    // TODO(b/34348946): do the actual implementation.
    _hidl_cb(Result::INVALID_STATE, false);
    return Void();
}

Return<Result> Tuner::setAnalogForced(bool isForced __unused) {
    // TODO(b/34348946): do the actual implementation.
    return Result::INVALID_STATE;
}

} // namespace implementation
}  // namespace V1_1
}  // namespace broadcastradio
}  // namespace hardware
}  // namespace android
