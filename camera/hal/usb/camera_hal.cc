/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/camera_hal.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>

#include "cros-camera/common.h"
#include "cros-camera/cros_camera_hal.h"
#include "cros-camera/udev_watcher.h"
#include "hal/usb/camera_characteristics.h"
#include "hal/usb/common_types.h"
#include "hal/usb/metadata_handler.h"
#include "hal/usb/quirks.h"
#include "hal/usb/stream_format.h"
#include "hal/usb/v4l2_camera_device.h"
#include "hal/usb/vendor_tag.h"

namespace cros {

namespace {

bool FillMetadata(const DeviceInfo& device_info,
                  android::CameraMetadata* static_metadata,
                  android::CameraMetadata* request_metadata) {
  if (MetadataHandler::FillDefaultMetadata(static_metadata, request_metadata) !=
      0) {
    return false;
  }

  if (MetadataHandler::FillMetadataFromDeviceInfo(device_info, static_metadata,
                                                  request_metadata) != 0) {
    return false;
  }

  SupportedFormats supported_formats =
      V4L2CameraDevice::GetDeviceSupportedFormats(device_info.device_path);
  SupportedFormats qualified_formats =
      GetQualifiedFormats(supported_formats, device_info.quirks);
  if (MetadataHandler::FillMetadataFromSupportedFormats(
          qualified_formats, device_info, static_metadata, request_metadata) !=
      0) {
    return false;
  }

  if (!device_info.usb_vid.empty()) {
    static_metadata->update(kVendorTagVendorId, device_info.usb_vid);
  }
  if (!device_info.usb_pid.empty()) {
    static_metadata->update(kVendorTagProductId, device_info.usb_pid);
  }
  static_metadata->update(kVendorTagDevicePath, device_info.device_path);
  static_metadata->update(kVendorTagModelName, V4L2CameraDevice::GetModelName(
                                                   device_info.device_path));

  return true;
}

bool IsVivid(udev_device* dev) {
  const char* product = udev_device_get_property_value(dev, "ID_V4L_PRODUCT");
  return product && strcmp(product, "vivid") == 0;
}

const char* GetPreferredPath(udev_device* dev) {
  if (IsVivid(dev)) {
    // Multiple vivid devices may have the same symlink at
    // /dev/v4l/by-path/platform-vivid.0-video-index0, so we use /dev/videoX
    // directly for vivid.
    return udev_device_get_devnode(dev);
  }

  for (udev_list_entry* entry = udev_device_get_devlinks_list_entry(dev);
       entry != nullptr; entry = udev_list_entry_get_next(entry)) {
    const char* name = udev_list_entry_get_name(entry);

    if (!name) {
      LOGF(WARNING) << "udev_list_entry_get_name failed";
      continue;
    }

    // The symlinks in /dev/v4l/by-path/ are generated by
    // 60-persistent-v4l.rules, and supposed to be persistent for built-in
    // cameras so we can safely reuse it across suspend/resume cycles, without
    // updating |path_to_id_| for them.
    // TODO(shik): Fix https://github.com/systemd/systemd/issues/10683 in the
    // upstream udev.
    if (base::StartsWith(name, "/dev/v4l/by-path/",
                         base::CompareCase::SENSITIVE)) {
      return name;
    }
  }

  return udev_device_get_devnode(dev);
}

std::string GetModelId(const DeviceInfo& info) {
  if (info.is_vivid) {
    return "vivid";
  }
  return base::JoinString({info.usb_vid, info.usb_pid}, ":");
}

}  // namespace

CameraHal::CameraHal()
    : task_runner_(nullptr),
      udev_watcher_(std::make_unique<UdevWatcher>(this, "video4linux")),
      cros_device_config_(CrosDeviceConfig::Get()) {
  thread_checker_.DetachFromThread();
}

CameraHal::~CameraHal() {
  udev_watcher_.reset();
}

int CameraHal::GetNumberOfCameras() const {
  return num_builtin_cameras_;
}

CameraHal& CameraHal::GetInstance() {
  static CameraHal camera_hal;
  return camera_hal;
}

CameraMojoChannelManager* CameraHal::GetMojoManagerInstance() {
  return mojo_manager_;
}

int CameraHal::OpenDevice(int id,
                          const hw_module_t* module,
                          hw_device_t** hw_device) {
  VLOGFID(1, id);
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!IsValidCameraId(id)) {
    LOGF(ERROR) << "Camera id " << id << " is invalid";
    return -EINVAL;
  }

  if (cameras_.find(id) != cameras_.end()) {
    LOGF(ERROR) << "Camera " << id << " is already opened";
    return -EBUSY;
  }
  if (!cameras_.empty() && cros_device_config_.model_name == "treeya360") {
    // It cannot open multiple cameras at the same time due to USB bandwidth
    // limitation (b/147333530).
    // TODO(shik): Use |conflicting_devices| to implement this logic after we
    // hook that in the ARC++ camera HAL shim.
    // TODO(shik): Add a new field in the unibuild schema instead of checking
    // model name here.
    LOGF(WARNING) << "Can't open Camera " << id << " because Camera "
                  << cameras_.begin()->first << " is already opened.";
    return -EUSERS;
  }
  cameras_[id].reset(
      new CameraClient(id, device_infos_[id], *static_metadata_[id].get(),
                       *request_template_[id].get(), module, hw_device));
  if (cameras_[id]->OpenDevice()) {
    cameras_.erase(id);
    return -ENODEV;
  }
  if (!task_runner_) {
    task_runner_ = base::ThreadTaskRunnerHandle::Get();
  }
  return 0;
}

bool CameraHal::IsValidCameraId(int id) {
  return device_infos_.find(id) != device_infos_.end();
}

int CameraHal::GetCameraInfo(int id, struct camera_info* info) {
  VLOGFID(1, id);
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!IsValidCameraId(id)) {
    LOGF(ERROR) << "Camera id " << id << " is invalid";
    return -EINVAL;
  }

  switch (device_infos_[id].lens_facing) {
    case ANDROID_LENS_FACING_FRONT:
      info->facing = CAMERA_FACING_FRONT;
      break;
    case ANDROID_LENS_FACING_BACK:
      info->facing = CAMERA_FACING_BACK;
      break;
    case ANDROID_LENS_FACING_EXTERNAL:
      info->facing = CAMERA_FACING_EXTERNAL;
      break;
    default:
      LOGF(ERROR) << "Unknown facing type: " << device_infos_[id].lens_facing;
      break;
  }
  info->orientation = device_infos_[id].sensor_orientation;
  info->device_version = CAMERA_DEVICE_API_VERSION_3_3;
  info->static_camera_characteristics = static_metadata_[id].get();
  info->resource_cost = 0;
  info->conflicting_devices = nullptr;
  info->conflicting_devices_length = 0;
  return 0;
}

int CameraHal::SetCallbacks(const camera_module_callbacks_t* callbacks) {
  VLOGF(1) << "New callbacks = " << callbacks;
  DCHECK(thread_checker_.CalledOnValidThread());

  callbacks_ = callbacks;

  // Some external cameras might be detected before SetCallbacks, we should
  // enumerate existing devices again after setting the callbacks.
  if (!udev_watcher_->EnumerateExistingDevices()) {
    LOGF(ERROR) << "Failed to EnumerateExistingDevices()";
  }

  return 0;
}

int CameraHal::Init() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!udev_watcher_->Start(base::ThreadTaskRunnerHandle::Get())) {
    LOGF(ERROR) << "Failed to Start()";
    return -ENODEV;
  }

  if (!udev_watcher_->EnumerateExistingDevices()) {
    LOGF(ERROR) << "Failed to EnumerateExistingDevices()";
    return -ENODEV;
  }

  // TODO(shik): possible race here. We may have 2 built-in cameras but just
  // detect one.
  if (CameraCharacteristics::ConfigFileExists() && num_builtin_cameras_ == 0) {
    LOGF(ERROR) << "Expect to find at least one camera if config file exists";
    return -ENODEV;
  }

  // TODO(shik): Some unibuild devices like vayne may have only user-facing
  // camera as "camera1" in |characteristics_|. It's a workaround for them until
  // we revise our config format. (b/111770440)
  if (device_infos_.size() == 1 && device_infos_.cbegin()->first == 1 &&
      num_builtin_cameras_ == 2) {
    LOGF(INFO) << "Renumber camera1 to camera0";

    device_infos_.emplace(0, std::move(device_infos_[1]));
    device_infos_.erase(1);
    device_infos_[0].camera_id = 0;

    DCHECK_EQ(path_to_id_.size(), 1);
    DCHECK_EQ(path_to_id_.begin()->second, 1);
    path_to_id_.begin()->second = 0;

    DCHECK_EQ(static_metadata_.size(), 1);
    DCHECK_EQ(static_metadata_.begin()->first, 1);
    static_metadata_.emplace(0, std::move(static_metadata_[1]));
    static_metadata_.erase(1);

    DCHECK_EQ(request_template_.size(), 1);
    DCHECK_EQ(request_template_.begin()->first, 1);
    request_template_.emplace(0, std::move(request_template_[1]));
    request_template_.erase(1);

    num_builtin_cameras_ = 1;
  }

  for (int i = 0; i < num_builtin_cameras_; i++) {
    if (!IsValidCameraId(i)) {
      LOGF(ERROR)
          << "The camera devices should be numbered 0 through N-1, but id = "
          << i << " is missing";
      return -ENODEV;
    }
  }

  next_external_camera_id_ = num_builtin_cameras_;

  if (!cros_device_config_.is_initialized) {
    LOGF(ERROR) << "Failed to initialize CrOS device config";
    // TODO(b/150578054): Return -ENODEV once the issue is fixed. For now, let's
    // ignore such error.
  }
  return 0;
}

void CameraHal::SetUp(CameraMojoChannelManager* mojo_manager) {
  mojo_manager_ = mojo_manager;
}

void CameraHal::TearDown() {
  mojo_manager_ = nullptr;
}

void CameraHal::CloseDeviceOnOpsThread(int id) {
  DCHECK(task_runner_);
  auto future = cros::Future<void>::Create(nullptr);
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&CameraHal::CloseDevice, base::Unretained(this), id,
                            base::RetainedRef(future)));
  future->Wait();
}

void CameraHal::CloseDevice(int id, scoped_refptr<cros::Future<void>> future) {
  VLOGFID(1, id);
  DCHECK(thread_checker_.CalledOnValidThread());

  if (cameras_.find(id) == cameras_.end()) {
    LOGF(ERROR) << "Failed to close camera device " << id
                << ": device is not opened";
    future->Set();
    return;
  }
  cameras_.erase(id);
  future->Set();
}

void CameraHal::OnDeviceAdded(ScopedUdevDevicePtr dev) {
  const char* path = GetPreferredPath(dev.get());
  if (!path) {
    LOGF(ERROR) << "udev_device_get_devnode failed";
    return;
  }

  const char* vid = "";
  const char* pid = "";

  bool is_vivid = IsVivid(dev.get());
  if (!is_vivid) {
    udev_device* parent_dev = udev_device_get_parent_with_subsystem_devtype(
        dev.get(), "usb", "usb_device");
    if (!parent_dev) {
      VLOGF(2) << "Non USB device is ignored";
      return;
    }

    vid = udev_device_get_sysattr_value(parent_dev, "idVendor");
    if (!vid) {
      LOGF(ERROR) << "Failed to get vid";
      return;
    }

    pid = udev_device_get_sysattr_value(parent_dev, "idProduct");
    if (!pid) {
      LOGF(ERROR) << "Failed to get pid";
      return;
    }
  }

  {
    // We have to check this because of:
    //  1. Limitation of libudev
    //  2. Reenumeration after SetCallbacks()
    //  3. Suspend/Resume
    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) {
      int id = it->second;
      const DeviceInfo& info = device_infos_[id];
      if (info.usb_vid == vid && info.usb_pid == pid) {
        VLOGF(1) << "Ignore " << path << " since it's already connected";
      } else {
        LOGF(ERROR) << "Device path conflict: " << path;
      }
      return;
    }
  }

  if (!V4L2CameraDevice::IsCameraDevice(path)) {
    VLOGF(1) << path << " is not a camera device";
    return;
  }

  if (is_vivid) {
    LOGF(INFO) << "New vivid camera device at " << path;
  } else {
    LOGF(INFO) << "New usb camera device at " << path << " vid: " << vid
               << " pid: " << pid;
  }

  DeviceInfo info;
  const DeviceInfo* info_ptr = characteristics_.Find(vid, pid);
  if (info_ptr != nullptr) {
    VLOGF(1) << "Found a built-in camera";
    info = *info_ptr;
    num_builtin_cameras_ = std::max(num_builtin_cameras_, info.camera_id + 1);
    if (info.constant_framerate_unsupported) {
      LOGF(WARNING) << "Camera module " << vid << ":" << pid
                    << " does not support constant frame rate";
    }
    // Checks constant frame rate can be enabled from V4L2 control.
    const bool cfr_supported_v4l2 =
        V4L2CameraDevice::IsConstantFrameRateSupported(path);
    if (cfr_supported_v4l2 != !info.constant_framerate_unsupported) {
      VLOGF(1) << "Camera characteristic constant_framerate_unsupported ("
               << info.constant_framerate_unsupported
               << ") doesn't match what queried from V4L2 ("
               << !cfr_supported_v4l2 << ") for camera module " << vid << ":"
               << pid << ". Set to unsupported.";
      info.constant_framerate_unsupported = true;
    }
  } else {
    VLOGF(1) << "Found an external camera";
    if (callbacks_ == nullptr) {
      VLOGF(1) << "No callbacks set, ignore it for now";
      return;
    }
  }

  info.device_path = path;
  info.usb_vid = vid;
  info.usb_pid = pid;
  info.is_vivid = is_vivid;
  info.power_line_frequency = V4L2CameraDevice::GetPowerLineFrequency(path);
  if (!is_vivid) {
    info.quirks |= GetQuirks(vid, pid);
  }

  // Mark the camera as v1 if it is a built-in camera and the CrOS device is
  // marked as a v1 device.
  if (info_ptr != nullptr && cros_device_config_.is_v1_device) {
    info.quirks |= kQuirkV1Device;
  }

  if (info_ptr == nullptr) {
    info.lens_facing = ANDROID_LENS_FACING_EXTERNAL;

    // Try to reuse the same id for the same camera.
    std::string model_id = GetModelId(info);
    std::set<int>& preferred_ids = previous_ids_[model_id];
    if (!preferred_ids.empty()) {
      info.camera_id = *preferred_ids.begin();
      previous_ids_.erase(previous_ids_.begin());
      VLOGF(1) << "Use the previous id " << info.camera_id << " for camera "
               << model_id;
    } else {
      info.camera_id = next_external_camera_id_++;
      VLOGF(1) << "Use a new id " << info.camera_id << " for camera "
               << model_id;
    }

    // Uses software timestamp from userspace for external cameras, because the
    // hardware timestamp is not reliable and sometimes even jump backwards.
    info.quirks |= kQuirkUserSpaceTimestamp;
  }

  android::CameraMetadata static_metadata, request_template;
  if (!FillMetadata(info, &static_metadata, &request_template)) {
    if (info.lens_facing == ANDROID_LENS_FACING_EXTERNAL) {
      LOGF(ERROR) << "FillMetadata failed, the new external "
                     "camera would be ignored";
      return;
    } else {
      LOGF(FATAL) << "FillMetadata failed for a built-in "
                     "camera, please check your camera config";
    }
  }

  path_to_id_[info.device_path] = info.camera_id;
  device_infos_[info.camera_id] = info;
  static_metadata_[info.camera_id] =
      ScopedCameraMetadata(static_metadata.release());
  request_template_[info.camera_id] =
      ScopedCameraMetadata(request_template.release());

  if (info.lens_facing == ANDROID_LENS_FACING_EXTERNAL) {
    callbacks_->camera_device_status_change(callbacks_, info.camera_id,
                                            CAMERA_DEVICE_STATUS_PRESENT);
  }
}

void CameraHal::OnDeviceRemoved(ScopedUdevDevicePtr dev) {
  const char* path = GetPreferredPath(dev.get());
  if (!path) {
    LOGF(ERROR) << "udev_device_get_devnode failed";
    return;
  }

  auto it = path_to_id_.find(path);
  if (it == path_to_id_.end()) {
    VLOGF(1) << "Cannot found id for " << path << ", ignore it";
    return;
  }

  int id = it->second;

  if (id < num_builtin_cameras_) {
    VLOGF(1) << "Camera " << id << "is a built-in camera, ignore it";
    return;
  }

  LOGF(INFO) << "Camera " << id << " at " << path << " removed";

  // TODO(shik): Handle this more gracefully, sometimes it even trigger a kernel
  // panic.
  if (cameras_.find(id) != cameras_.end()) {
    LOGF(WARNING)
        << "Unplug an opening camera, exit the camera service to cleanup";
    // Upstart will start the service again.
    _exit(EIO);
  }

  previous_ids_[GetModelId(device_infos_[id])].insert(id);

  path_to_id_.erase(it);
  device_infos_.erase(id);
  static_metadata_.erase(id);
  request_template_.erase(id);

  if (callbacks_) {
    callbacks_->camera_device_status_change(callbacks_, id,
                                            CAMERA_DEVICE_STATUS_NOT_PRESENT);
  }
}

static int camera_device_open(const hw_module_t* module,
                              const char* name,
                              hw_device_t** device) {
  VLOGF_ENTER();
  // Make sure hal adapter loads the correct symbol.
  if (module != &HAL_MODULE_INFO_SYM.common) {
    LOGF(ERROR) << std::hex << "Invalid module 0x" << module << " expected 0x"
                << &HAL_MODULE_INFO_SYM.common;
    return -EINVAL;
  }

  char* nameEnd;
  int id = strtol(name, &nameEnd, 10);
  if (*nameEnd != '\0') {
    LOGF(ERROR) << "Invalid camera name " << name;
    return -EINVAL;
  }

  return CameraHal::GetInstance().OpenDevice(id, module, device);
}

static int get_number_of_cameras() {
  return CameraHal::GetInstance().GetNumberOfCameras();
}

static int get_camera_info(int id, struct camera_info* info) {
  return CameraHal::GetInstance().GetCameraInfo(id, info);
}

static int set_callbacks(const camera_module_callbacks_t* callbacks) {
  return CameraHal::GetInstance().SetCallbacks(callbacks);
}

static void get_vendor_tag_ops(vendor_tag_ops_t* ops) {
  ops->get_all_tags = VendorTagOps::GetAllTags;
  ops->get_tag_count = VendorTagOps::GetTagCount;
  ops->get_section_name = VendorTagOps::GetSectionName;
  ops->get_tag_name = VendorTagOps::GetTagName;
  ops->get_tag_type = VendorTagOps::GetTagType;
}

static int open_legacy(const struct hw_module_t* /*module*/,
                       const char* /*id*/,
                       uint32_t /*halVersion*/,
                       struct hw_device_t** /*device*/) {
  return -ENOSYS;
}

static int set_torch_mode(const char* /*camera_id*/, bool /*enabled*/) {
  return -ENOSYS;
}

static int init() {
  return CameraHal::GetInstance().Init();
}

static void set_up(CameraMojoChannelManager* mojo_manager) {
  CameraHal::GetInstance().SetUp(mojo_manager);
}

static void tear_down() {
  CameraHal::GetInstance().TearDown();
}

int camera_device_close(struct hw_device_t* hw_device) {
  camera3_device_t* cam_dev = reinterpret_cast<camera3_device_t*>(hw_device);
  CameraClient* cam = static_cast<CameraClient*>(cam_dev->priv);
  if (!cam) {
    LOGF(ERROR) << "Camera device is NULL";
    return -EIO;
  }
  cam_dev->priv = NULL;
  int ret = cam->CloseDevice();
  CameraHal::GetInstance().CloseDeviceOnOpsThread(cam->GetId());
  return ret;
}

}  // namespace cros

static hw_module_methods_t gCameraModuleMethods = {
    .open = cros::camera_device_open};

camera_module_t HAL_MODULE_INFO_SYM CROS_CAMERA_EXPORT = {
    .common = {.tag = HARDWARE_MODULE_TAG,
               .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
               .hal_api_version = HARDWARE_HAL_API_VERSION,
               .id = CAMERA_HARDWARE_MODULE_ID,
               .name = "V4L2 UVC Camera HAL v3",
               .author = "The Chromium OS Authors",
               .methods = &gCameraModuleMethods,
               .dso = NULL,
               .reserved = {0}},
    .get_number_of_cameras = cros::get_number_of_cameras,
    .get_camera_info = cros::get_camera_info,
    .set_callbacks = cros::set_callbacks,
    .get_vendor_tag_ops = cros::get_vendor_tag_ops,
    .open_legacy = cros::open_legacy,
    .set_torch_mode = cros::set_torch_mode,
    .init = cros::init,
    .reserved = {0}};

cros::cros_camera_hal_t CROS_CAMERA_HAL_INFO_SYM CROS_CAMERA_EXPORT = {
    .set_up = cros::set_up, .tear_down = cros::tear_down};
