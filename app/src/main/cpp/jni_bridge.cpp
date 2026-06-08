#include <jni.h>
#include <android/log.h>
#include <memory>
#include <string>
#include "chord_engine.h"

#define LOG_TAG "JNI_Bridge"

static std::unique_ptr<ChordEngine> g_engine;
static JavaVM* g_jvm = nullptr;
static jobject g_listener = nullptr;
static jmethodID g_onChord_method = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_acordes_detector_ChordDetector_nativeInit(JNIEnv* env, jobject thiz) {
    g_engine = std::make_unique<ChordEngine>();
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Engine initialized");
}

extern "C" JNIEXPORT void JNICALL
Java_com_acordes_detector_ChordDetector_nativeStart(JNIEnv* env, jobject thiz, jobject listener) {
    if (!g_engine) return;

    // Keep a global reference to the listener object
    if (g_listener) env->DeleteGlobalRef(g_listener);
    g_listener = env->NewGlobalRef(listener);

    jclass cls = env->GetObjectClass(listener);
    g_onChord_method = env->GetMethodID(cls, "onChord", "(IIFFLjava/lang/String;Ljava/lang/String;)V");

    g_engine->start([](const ChordResult& result) {
        JNIEnv* env = nullptr;
        bool attached = false;

        jint status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            g_jvm->AttachCurrentThread(&env, nullptr);
            attached = true;
        }
        if (!env || !g_listener || !g_onChord_method) {
            if (attached) g_jvm->DetachCurrentThread();
            return;
        }

        const char* chord_cstr = chordName(result.chordIndex);
        const char* key_cstr   = keyName(result.keyIndex);
        jstring jchord = env->NewStringUTF(chord_cstr);
        jstring jkey   = env->NewStringUTF(key_cstr);

        env->CallVoidMethod(g_listener, g_onChord_method,
                            (jint)result.chordIndex,
                            (jint)result.keyIndex,
                            (jfloat)result.confidence,
                            (jfloat)result.rms,
                            jchord, jkey);

        env->DeleteLocalRef(jchord);
        env->DeleteLocalRef(jkey);

        if (attached) g_jvm->DetachCurrentThread();
    });
}

extern "C" JNIEXPORT void JNICALL
Java_com_acordes_detector_ChordDetector_nativeStop(JNIEnv* env, jobject thiz) {
    if (g_engine) g_engine->stop();
    if (g_listener) {
        env->DeleteGlobalRef(g_listener);
        g_listener = nullptr;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_acordes_detector_ChordDetector_nativeDestroy(JNIEnv* env, jobject thiz) {
    if (g_engine) {
        g_engine->stop();
        g_engine.reset();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_acordes_detector_ChordDetector_nativeIsRunning(JNIEnv* env, jobject thiz) {
    if (!g_engine) return JNI_FALSE;
    return g_engine->isRunning() ? JNI_TRUE : JNI_FALSE;
}
