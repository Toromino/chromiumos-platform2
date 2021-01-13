/*
 * Copyright (C) 2019 MediaTek Inc.
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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_KERNEL_IMGSENSOR_KD_IMGSENSOR_ERRCODE_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_KERNEL_IMGSENSOR_KD_IMGSENSOR_ERRCODE_H_

/* @ the same as camera_custom_errocode.h */
typedef enum {
  ERROR_NONE = 0,
  ERROR_MSDK_IS_ACTIVED,
  ERROR_INVALID_DRIVER_MOD_ID,
  ERROR_INVALID_FEATURE_ID,
  ERROR_INVALID_SCENARIO_ID,
  ERROR_INVALID_CTRL_CODE,
  ERROR_VIDEO_ENCODER_BUSY,
  ERROR_INVALID_PARA,
  ERROR_OUT_OF_BUFFER_NUMBER,
  ERROR_INVALID_ISP_STATE,
  ERROR_INVALID_MSDK_STATE,
  ERROR_PHY_VIR_MEM_MAP_FAIL,
  ERROR_ENQUEUE_BUFFER_NOT_FOUND,
  ERROR_MSDK_BUFFER_ALREADY_INIT,
  ERROR_MSDK_BUFFER_OUT_OF_MEMORY,
  ERROR_SENSOR_POWER_ON_FAIL,
  ERROR_SENSOR_CONNECT_FAIL,
  ERROR_SENSOR_FEATURE_NOT_IMPLEMENT,
  ERROR_MSDK_IO_CONTROL_CODE,
  ERROR_MSDK_IO_CONTROL_MSG_QUEUE_OPEN_FAIL,
  ERROR_DRIVER_INIT_FAIL,
  ERROR_WRONG_NVRAM_CAMERA_VERSION,
  ERROR_NVRAM_CAMERA_FILE_FAIL,
  ERROR_IMAGE_DECODE_FAIL,
  ERROR_IMAGE_ENCODE_FAIL,
  ERROR_LED_FLASH_POWER_ON_FAIL,
  ERROR_MSDK_NOT_ALLOW_BY_MM_APP_MGR,
  ERROR_LENS_NOT_SUPPORT,
  ERROR_FLASH_LIGHT_NOT_SUPPORT,
  ERROR_FACE_DETECTION_NOT_SUPPORT,
  ERROR_PANORAMA_NOT_SUPPORT,
  ERROR_MAX
} CUSTOM_CAMERA_ERROR_CODE_ENUM;

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_KERNEL_IMGSENSOR_KD_IMGSENSOR_ERRCODE_H_