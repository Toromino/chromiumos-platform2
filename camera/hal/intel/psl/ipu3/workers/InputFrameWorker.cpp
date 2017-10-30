/*
 * Copyright (C) 2016-2018 Intel Corporation.
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

#define LOG_TAG "InputFrameWorker"

#include "InputFrameWorker.h"

#include "PerformanceTraces.h"
#include "NodeTypes.h"

namespace android {
namespace camera2 {

InputFrameWorker::InputFrameWorker(std::shared_ptr<cros::V4L2VideoNode> node,
        int cameraId, size_t pipelineDepth) :
        /* Keep the same number of buffers as ISYS. */
        FrameWorker(node, cameraId, pipelineDepth + 1, "InputFrameWorker")
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    mPollMe = true;
}

InputFrameWorker::~InputFrameWorker()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
}

status_t InputFrameWorker::configure(std::shared_ptr<GraphConfig> & /*config*/)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    status_t ret = mNode->GetFormat(&mFormat);
    if (ret != OK)
        return ret;

    ret = setWorkerDeviceBuffers(getDefaultMemoryType(IMGU_NODE_INPUT));
    if (ret != OK)
        return ret;

    return OK;
}

status_t InputFrameWorker::prepareRun(std::shared_ptr<DeviceMessage> msg)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    status_t status = OK;
    int memType = mNode->GetMemoryType();
    int index = msg->pMsg.rawNonScaledBuffer->v4l2Buf.Index();

    if (memType == V4L2_MEMORY_USERPTR) {
        unsigned long userptr = (long unsigned int)msg->pMsg.rawNonScaledBuffer->buf->data();
        mBuffers[index].Userptr(userptr);
    } else if (memType == V4L2_MEMORY_DMABUF) {
        int fd = msg->pMsg.rawNonScaledBuffer->buf->dmaBufFd();
        mBuffers[index].SetFd(fd, 0);
        CheckError((mBuffers[index].Fd(0) < 0), BAD_VALUE, "@%s invalid fd(%d) passed from isys.\n",
            __func__, mBuffers[index].Fd(0));
    } else {
        LOGE("@%s unsupported memory type %d.", __func__, memType);
        return BAD_VALUE;
    }
    status |= mNode->PutFrame(&mBuffers[index]);

    msg->pMsg.processingSettings->request->setSeqenceId(msg->pMsg.rawNonScaledBuffer->v4l2Buf.Sequence());
    PERFORMANCE_HAL_ATRACE_PARAM1("seqId", msg->pMsg.rawNonScaledBuffer->v4l2Buf.Sequence());

    return status;
}

status_t InputFrameWorker::run()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    return OK;
}

status_t InputFrameWorker::postRun()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    cros::V4L2Buffer outBuf;
    status_t status = mNode->GrabFrame(&outBuf);

    return (status < 0) ? status : OK;
}


} /* namespace camera2 */
} /* namespace android */
