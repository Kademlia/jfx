/*
 * Copyright (c) 2010, 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <com_sun_media_jfxmediaimpl_platform_gstreamer_GSTMedia.h>
#include <com_sun_media_jfxmedia_control_VideoFormat_FormatTypes.h>

#include <Common/ProductFlags.h>
#include <Common/VSMemory.h>
#include <MediaManagement/Media.h>
#include <MediaManagement/MediaManager.h>
#include <PipelineManagement/PipelineOptions.h>
#include <PipelineManagement/VideoFrame.h>
#include <Locator/Locator.h>
#include <Locator/LocatorStream.h>
#include <jni/JniUtils.h>
#include <jni/JavaInputStreamCallbacks.h>
#include <jfxmedia_errors.h>
#include <Utils/LowLevelPerf.h>
#include <memory>

using namespace std;

#define HLS_PROP_HAS_AUDIO_EXT_STREAM 6

//*************************************************************************************************
//********** com.sun.media.jfxmediaimpl.Media JNI support functions
//*************************************************************************************************

#ifdef __cplusplus
extern "C" {
#endif

    static jint InitMedia(JNIEnv *env, CPipelineOptions* pOptions, jobject jLocator, jstring jContentType, jlong jSizeHint,
                          jlongArray jlMediaHandle)
    {
        CMedia*         pMedia = NULL;
        CMediaManager*  pManager = NULL;
        uint32_t        uErrCode = CMediaManager::GetInstance(&pManager);

        if (ERROR_NONE != uErrCode)
            return uErrCode;
        if (NULL == pManager)
            return ERROR_MANAGER_NULL;

        //***** pre-conditions
        jstring jLocation = CLocator::LocatorGetStringLocation(env, jLocator);
        if (NULL == jLocation)
            return ERROR_MEMORY_ALLOCATION;
        struct UtfCharsDeleter {
            JNIEnv* env;
            jstring value;
            void operator()(const char* chars) const { env->ReleaseStringUTFChars(value, chars); }
        };
        std::unique_ptr<const char, UtfCharsDeleter> content(
                env->GetStringUTFChars(jContentType, NULL), {env, jContentType});
        if (!content)
            return ERROR_MEMORY_ALLOCATION;
        std::unique_ptr<const char, UtfCharsDeleter> location(
                env->GetStringUTFChars(jLocation, NULL), {env, jLocation});
        if (!location)
            return ERROR_MEMORY_ALLOCATION;
        //***** Create a new native locator object
        std::unique_ptr<CJavaInputStreamCallbacks> callbacks(new (nothrow) CJavaInputStreamCallbacks());
        if (!callbacks)
            return ERROR_MEMORY_ALLOCATION;
        jobject jConnectionHolder = CLocator::CreateConnectionHolder(env, jLocator);
        if (NULL == jConnectionHolder)
            return ERROR_MEMORY_ALLOCATION;

        if (!callbacks->Init(env, jConnectionHolder))
            return ERROR_MEDIA_CREATION;

        std::unique_ptr<CLocatorStream> locator(new(nothrow) CLocatorStream(callbacks.get(), content.get(), location.get(), (int64_t)jSizeHint));
        content.reset();
        location.reset();

        if (!locator)
        {
            return ERROR_MEMORY_ALLOCATION;
        }
        callbacks.release(); // The locator owns callbacks until source creation.

        // Load any additional streams if needed.
        // HLS_PROP_HAS_AUDIO_EXT_STREAM
        int hasAudioStream = locator->GetCallbacks()->Property(HLS_PROP_HAS_AUDIO_EXT_STREAM, 0);
        if (hasAudioStream)
        {
            std::unique_ptr<CJavaInputStreamCallbacks> audioStreamCallbacks(
                    new (nothrow) CJavaInputStreamCallbacks());
            if (!audioStreamCallbacks)
                return ERROR_MEMORY_ALLOCATION;
            jobject jAudioStreamConnectionHolder =
                    CLocator::GetAudioStreamConnectionHolder(env, jLocator, jConnectionHolder);
            if (NULL == jAudioStreamConnectionHolder)
            {
                return ERROR_MEMORY_ALLOCATION;
            }

            if (!audioStreamCallbacks->Init(env, jAudioStreamConnectionHolder))
            {
                return ERROR_MEDIA_CREATION;
            }

            locator->SetAudioCallbacks(audioStreamCallbacks.release());
        }

        //***** Create the media object
        uErrCode  = pManager->CreatePlayer(locator.get(), pOptions, &pMedia);

        //***** return
        if (ERROR_NONE == uErrCode)
        {
            if (CMedia::IsValid(pMedia))
            {
                jlong lMediaHandle = (jlong)ptr_to_jlong(pMedia);
                env->SetLongArrayRegion(jlMediaHandle, 0, 1, &lMediaHandle);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
            }
            else
            {
                uErrCode = ERROR_MEDIA_INVALID;
            }
        }

        // Clean up
        if (ERROR_NONE != uErrCode)
        {
            if (NULL != pMedia)
                delete pMedia;
        }

        return uErrCode;
    }

    /**
     * GSTMedia_gstInitNativeMedia()
     *
     * Creates a native media reference for the resource string.
     *
     * @return  Media reference.  This reference must be used when calling GSTMediaPlayer function.
     */
    JNIEXPORT jint JNICALL Java_com_sun_media_jfxmediaimpl_platform_gstreamer_GSTMedia_gstInitNativeMedia
    (JNIEnv *env, jobject obj, jobject jLocator, jstring jContentType, jlong jSizeHint, jlongArray jlMediaHandle)
    {
        LOWLEVELPERF_EXECTIMESTART("gstInitNativeMediaToSendToJavaPlayerStateEventPaused");
        LOWLEVELPERF_EXECTIMESTART("gstInitNativeMedia()");
        uint32_t result = InitMedia(env, NULL, jLocator, jContentType, jSizeHint, jlMediaHandle);
        LOWLEVELPERF_EXECTIMESTOP("gstInitNativeMedia()");

        return result;
    }

    /*
    * Class:     com_sun_media_jfxmediaimpl_platform_gstreamer_GSTMedia
    * Method:    gstDispose
    * Signature: (J)V
    */
    JNIEXPORT void JNICALL Java_com_sun_media_jfxmediaimpl_platform_gstreamer_GSTMedia_gstDispose
    (JNIEnv *env, jobject obj, jlong ref_media)
    {
        LOWLEVELPERF_EXECTIMESTART("gstDispose()");

        CMedia* pMedia = (CMedia*)jlong_to_ptr(ref_media);

        if (pMedia != NULL)
        {
            delete pMedia;
            pMedia = NULL;
        }

        LOWLEVELPERF_EXECTIMESTOP("gstDispose()");
    }

#ifdef __cplusplus
}
#endif
