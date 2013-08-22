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

// Uncomment to enable verbose debug output
#define LOG_NDEBUG 0

#undef LOG_TAG
#define LOG_TAG "SurfaceTextureClientHybris"

#include <hybris/media/surface_texture_client_hybris.h>
#include "surface_texture_client_hybris_priv.h"

#include <ui/GraphicBuffer.h>
#include <utils/Log.h>
#include <ui/Region.h>
#include <gui/Surface.h>
#include <gui/SurfaceTextureClient.h>

#define REPORT_FUNCTION() ALOGV("%s \n", __PRETTY_FUNCTION__);

using namespace android;

// ----- Begin _SurfaceTextureClientHybris API ----- //

static inline _SurfaceTextureClientHybris *get_internal_stcu(SurfaceTextureClientHybris stc)
{
    if (stc == NULL)
    {
        ALOGE("stc must not be NULL");
        return NULL;
    }

    _SurfaceTextureClientHybris *s = static_cast<_SurfaceTextureClientHybris*>(stc);
    assert(s->refcount >= 1);

    return s;
}

_SurfaceTextureClientHybris::_SurfaceTextureClientHybris()
{
    REPORT_FUNCTION()

    // setBufferCount(5);
}

_SurfaceTextureClientHybris::_SurfaceTextureClientHybris(const _SurfaceTextureClientHybris &stch)
    : SurfaceTextureClient::SurfaceTextureClient(),
      Singleton<_SurfaceTextureClientHybris>::Singleton(),
      refcount(stch.refcount)
{
    REPORT_FUNCTION()
}

_SurfaceTextureClientHybris::_SurfaceTextureClientHybris(const sp<ISurfaceTexture> &st)
    : SurfaceTextureClient::SurfaceTextureClient(st),
      refcount(1)
{
    REPORT_FUNCTION()

    // setBufferCount(5);
}

_SurfaceTextureClientHybris::~_SurfaceTextureClientHybris()
{
    REPORT_FUNCTION()

    refcount = 1;
}

int _SurfaceTextureClientHybris::dequeueBuffer(ANativeWindowBuffer** buffer, int* fenceFd)
{
    return SurfaceTextureClient::dequeueBuffer(buffer, fenceFd);
}

int _SurfaceTextureClientHybris::queueBuffer(ANativeWindowBuffer* buffer, int fenceFd)
{
    return SurfaceTextureClient::queueBuffer(buffer, fenceFd);
}

void _SurfaceTextureClientHybris::setISurfaceTexture(const sp<ISurfaceTexture>& surface_texture)
{
    SurfaceTextureClient::setISurfaceTexture(surface_texture);
}

// ----- End _SurfaceTextureClientHybris API ----- //

SurfaceTextureClientHybris surface_texture_client_create(EGLNativeWindowType native_window)
{
    REPORT_FUNCTION()

    sp<Surface> surface = static_cast<Surface*>(native_window);
    _SurfaceTextureClientHybris::getInstance().setISurfaceTexture(surface->getSurfaceTexture());
    // TODO: Get rid of this return value since it's no longer needed with the singleton
    return NULL;
}

// TODO: Get rid of these instance ref/unref/del methods - no longer necessary with the singleton
void surface_texture_client_destroy(SurfaceTextureClientHybris stc)
{
    REPORT_FUNCTION()

    _SurfaceTextureClientHybris *s = get_internal_stcu(stc);
    if (s == NULL)
        return;

    if (s->refcount)
        return;

    delete s;
}

void surface_texture_client_ref(SurfaceTextureClientHybris stc)
{
    REPORT_FUNCTION()

    _SurfaceTextureClientHybris *s = get_internal_stcu(stc);
    if (s == NULL)
        return;

    s->refcount++;
}

void surface_texture_client_unref(SurfaceTextureClientHybris stc)
{
    REPORT_FUNCTION()

    _SurfaceTextureClientHybris *s = get_internal_stcu(stc);
    if (s == NULL)
        return;

    if (s->refcount)
        s->refcount--;
}

void surface_texture_client_set_surface_texture(SurfaceTextureClientHybris stc, EGLNativeWindowType native_window)
{
    _SurfaceTextureClientHybris *s = get_internal_stcu(stc);
    if (s == NULL)
        return;

    if (native_window == NULL)
    {
        ALOGE("native_window must not be NULL");
        return;
    }

    sp<Surface> surface = static_cast<Surface*>(native_window);
    s->setISurfaceTexture(surface->getSurfaceTexture());
}

ANDROID_SINGLETON_STATIC_INSTANCE(_SurfaceTextureClientHybris)
