// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the boilerplate implementation of the IAllocator HAL interface,
// generated by the hidl-gen tool and then modified for use on Chrome OS.
// Modifications include:
// - Removal of non boiler plate client and server related code.
// - Reformatting to meet the Chrome OS coding standards.
//
// Originally generated with the command:
// $ hidl-gen -o output -L c++-adapter -r android.hardware:hardware/interfaces \
//   android.hardware.neuralnetworks@1.3

#include <android/hardware/neuralnetworks/1.3/APreparedModel.h>
#include <android/hardware/neuralnetworks/1.0/AExecutionCallback.h>
#include <android/hardware/neuralnetworks/1.0/APreparedModel.h>
#include <android/hardware/neuralnetworks/1.2/ABurstCallback.h>
#include <android/hardware/neuralnetworks/1.2/ABurstContext.h>
#include <android/hardware/neuralnetworks/1.2/AExecutionCallback.h>
#include <android/hardware/neuralnetworks/1.2/APreparedModel.h>
#include <android/hardware/neuralnetworks/1.3/AExecutionCallback.h>
#include <android/hardware/neuralnetworks/1.3/AFencedExecutionCallback.h>
#include <android/hardware/neuralnetworks/1.3/IPreparedModel.h>
#include <hidladapter/HidlBinderAdapter.h>

namespace android {
namespace hardware {
namespace neuralnetworks {
namespace V1_3 {

APreparedModel::APreparedModel(
    const ::android::sp<
        ::android::hardware::neuralnetworks::V1_3::IPreparedModel>& impl)
    : mImpl(impl) {
}  // Methods from ::android::hardware::neuralnetworks::V1_0::IPreparedModel
   // follow.
::android::hardware::Return<
    ::android::hardware::neuralnetworks::V1_0::ErrorStatus>
APreparedModel::execute(
    const ::android::hardware::neuralnetworks::V1_0::Request& request,
    const ::android::sp<
        ::android::hardware::neuralnetworks::V1_0::IExecutionCallback>&
        callback) {
  auto _hidl_out = mImpl->execute(
      request,
      static_cast<::android::sp<
          ::android::hardware::neuralnetworks::V1_0::IExecutionCallback>>(
          ::android::hardware::neuralnetworks::V1_0::IExecutionCallback::
              castFrom(::android::hardware::details::adaptWithDefault(
                  static_cast<
                      ::android::sp<::android::hardware::neuralnetworks::V1_0::
                                        IExecutionCallback>>(callback),
                  [&] {
                    return new ::android::hardware::neuralnetworks::V1_0::
                        AExecutionCallback(callback);
                  }))));
  if (!_hidl_out.isOkUnchecked()) {
    return _hidl_out;
  }
  return _hidl_out;
}

// Methods from ::android::hardware::neuralnetworks::V1_2::IPreparedModel
// follow.
::android::hardware::Return<
    ::android::hardware::neuralnetworks::V1_0::ErrorStatus>
APreparedModel::execute_1_2(
    const ::android::hardware::neuralnetworks::V1_0::Request& request,
    ::android::hardware::neuralnetworks::V1_2::MeasureTiming measure,
    const ::android::sp<
        ::android::hardware::neuralnetworks::V1_2::IExecutionCallback>&
        callback) {
  auto _hidl_out = mImpl->execute_1_2(
      request, measure,
      static_cast<::android::sp<
          ::android::hardware::neuralnetworks::V1_2::IExecutionCallback>>(
          ::android::hardware::neuralnetworks::V1_2::IExecutionCallback::
              castFrom(::android::hardware::details::adaptWithDefault(
                  static_cast<
                      ::android::sp<::android::hardware::neuralnetworks::V1_2::
                                        IExecutionCallback>>(callback),
                  [&] {
                    return new ::android::hardware::neuralnetworks::V1_2::
                        AExecutionCallback(callback);
                  }))));
  if (!_hidl_out.isOkUnchecked()) {
    return _hidl_out;
  }
  return _hidl_out;
}
::android::hardware::Return<void> APreparedModel::executeSynchronously(
    const ::android::hardware::neuralnetworks::V1_0::Request& request,
    ::android::hardware::neuralnetworks::V1_2::MeasureTiming measure,
    executeSynchronously_cb _hidl_cb) {
  executeSynchronously_cb _hidl_cb_wrapped =
      [&](::android::hardware::neuralnetworks::V1_0::ErrorStatus status,
          const ::android::hardware::hidl_vec<
              ::android::hardware::neuralnetworks::V1_2::OutputShape>&
              outputShapes,
          const ::android::hardware::neuralnetworks::V1_2::Timing& timing) {
        return _hidl_cb(status, outputShapes, timing);
      };
  auto _hidl_out =
      mImpl->executeSynchronously(request, measure, _hidl_cb_wrapped);
  return _hidl_out;
}
::android::hardware::Return<void> APreparedModel::configureExecutionBurst(
    const ::android::sp<
        ::android::hardware::neuralnetworks::V1_2::IBurstCallback>& callback,
    const ::android::hardware::MQDescriptorSync<
        ::android::hardware::neuralnetworks::V1_2::FmqRequestDatum>&
        requestChannel,
    const ::android::hardware::MQDescriptorSync<
        ::android::hardware::neuralnetworks::V1_2::FmqResultDatum>&
        resultChannel,
    configureExecutionBurst_cb _hidl_cb) {
  configureExecutionBurst_cb _hidl_cb_wrapped =
      [&](::android::hardware::neuralnetworks::V1_0::ErrorStatus status,
          const ::android::sp<
              ::android::hardware::neuralnetworks::V1_2::IBurstContext>&
              context) {
        return _hidl_cb(
            status,
            static_cast<::android::sp<
                ::android::hardware::neuralnetworks::V1_2::IBurstContext>>(
                ::android::hardware::neuralnetworks::V1_2::IBurstContext::
                    castFrom(::android::hardware::details::adaptWithDefault(
                        static_cast<
                            ::android::sp<::android::hardware::neuralnetworks::
                                              V1_2::IBurstContext>>(context),
                        [&] {
                          return new ::android::hardware::neuralnetworks::V1_2::
                              ABurstContext(context);
                        }))));
      };
  auto _hidl_out = mImpl->configureExecutionBurst(
      static_cast<::android::sp<
          ::android::hardware::neuralnetworks::V1_2::IBurstCallback>>(
          ::android::hardware::neuralnetworks::V1_2::IBurstCallback::castFrom(
              ::android::hardware::details::adaptWithDefault(
                  static_cast<
                      ::android::sp<::android::hardware::neuralnetworks::V1_2::
                                        IBurstCallback>>(callback),
                  [&] {
                    return new ::android::hardware::neuralnetworks::V1_2::
                        ABurstCallback(callback);
                  }))),
      requestChannel, resultChannel, _hidl_cb_wrapped);
  return _hidl_out;
}

// Methods from ::android::hardware::neuralnetworks::V1_3::IPreparedModel
// follow.
::android::hardware::Return<
    ::android::hardware::neuralnetworks::V1_3::ErrorStatus>
APreparedModel::execute_1_3(
    const ::android::hardware::neuralnetworks::V1_3::Request& request,
    ::android::hardware::neuralnetworks::V1_2::MeasureTiming measure,
    const ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint&
        deadline,
    const ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration&
        loopTimeoutDuration,
    const ::android::sp<
        ::android::hardware::neuralnetworks::V1_3::IExecutionCallback>&
        callback) {
  auto _hidl_out = mImpl->execute_1_3(
      request, measure, deadline, loopTimeoutDuration,
      static_cast<::android::sp<
          ::android::hardware::neuralnetworks::V1_3::IExecutionCallback>>(
          ::android::hardware::neuralnetworks::V1_3::IExecutionCallback::
              castFrom(::android::hardware::details::adaptWithDefault(
                  static_cast<
                      ::android::sp<::android::hardware::neuralnetworks::V1_3::
                                        IExecutionCallback>>(callback),
                  [&] {
                    return new ::android::hardware::neuralnetworks::V1_3::
                        AExecutionCallback(callback);
                  }))));
  if (!_hidl_out.isOkUnchecked()) {
    return _hidl_out;
  }
  return _hidl_out;
}
::android::hardware::Return<void> APreparedModel::executeSynchronously_1_3(
    const ::android::hardware::neuralnetworks::V1_3::Request& request,
    ::android::hardware::neuralnetworks::V1_2::MeasureTiming measure,
    const ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint&
        deadline,
    const ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration&
        loopTimeoutDuration,
    executeSynchronously_1_3_cb _hidl_cb) {
  executeSynchronously_1_3_cb _hidl_cb_wrapped =
      [&](::android::hardware::neuralnetworks::V1_3::ErrorStatus status,
          const ::android::hardware::hidl_vec<
              ::android::hardware::neuralnetworks::V1_2::OutputShape>&
              outputShapes,
          const ::android::hardware::neuralnetworks::V1_2::Timing& timing) {
        return _hidl_cb(status, outputShapes, timing);
      };
  auto _hidl_out = mImpl->executeSynchronously_1_3(
      request, measure, deadline, loopTimeoutDuration, _hidl_cb_wrapped);
  return _hidl_out;
}
::android::hardware::Return<void> APreparedModel::executeFenced(
    const ::android::hardware::neuralnetworks::V1_3::Request& request,
    const ::android::hardware::hidl_vec<::android::hardware::hidl_handle>&
        waitFor,
    ::android::hardware::neuralnetworks::V1_2::MeasureTiming measure,
    const ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint&
        deadline,
    const ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration&
        loopTimeoutDuration,
    const ::android::hardware::neuralnetworks::V1_3::OptionalTimeoutDuration&
        duration,
    executeFenced_cb _hidl_cb) {
  executeFenced_cb _hidl_cb_wrapped =
      [&](::android::hardware::neuralnetworks::V1_3::ErrorStatus status,
          const ::android::hardware::hidl_handle& syncFence,
          const ::android::sp<::android::hardware::neuralnetworks::V1_3::
                                  IFencedExecutionCallback>& callback) {
        return _hidl_cb(
            status, syncFence,
            static_cast<::android::sp<::android::hardware::neuralnetworks::
                                          V1_3::IFencedExecutionCallback>>(
                ::android::hardware::neuralnetworks::V1_3::
                    IFencedExecutionCallback::castFrom(
                        ::android::hardware::details::adaptWithDefault(
                            static_cast<::android::sp<
                                ::android::hardware::neuralnetworks::V1_3::
                                    IFencedExecutionCallback>>(callback),
                            [&] {
                              return new ::android::hardware::neuralnetworks::
                                  V1_3::AFencedExecutionCallback(callback);
                            }))));
      };
  auto _hidl_out =
      mImpl->executeFenced(request, waitFor, measure, deadline,
                           loopTimeoutDuration, duration, _hidl_cb_wrapped);
  return _hidl_out;
}

// Methods from ::android::hidl::base::V1_0::IBase follow.

}  // namespace V1_3
}  // namespace neuralnetworks
}  // namespace hardware
}  // namespace android