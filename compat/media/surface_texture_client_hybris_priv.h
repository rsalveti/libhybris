/*
 * Copyright (C) 2013 Canonical Ltd
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
 *
 * Authored by: Jim Hodapp <jim.hodapp@canonical.com>
 */

#include <gui/SurfaceTextureClient.h>

struct _SurfaceTextureClientHybris : public android::SurfaceTextureClient
{
public:
    explicit _SurfaceTextureClientHybris(const android::sp<android::ISurfaceTexture> &st);
    ~_SurfaceTextureClientHybris();

    int dequeueBuffer(ANativeWindowBuffer** buffer, int* fenceFd);
    int queueBuffer(ANativeWindowBuffer* buffer, int fenceFd);
    void setISurfaceTexture(const android::sp<android::ISurfaceTexture>& surface_texture);

    unsigned int refcount;
};

