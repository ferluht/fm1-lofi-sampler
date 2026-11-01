/* JNI bridge to the shared codec/pack code in ../../../../../core (the same headers the firmware uses). */
#include <jni.h>
#include "lofi_pack.h"

JNIEXPORT jbyteArray JNICALL Java_com_ferluht_lofi_Lofi_buildPack(JNIEnv *env, jclass cls, jstring jname, jint scale, jint scaleRoot, jobjectArray jpcm,
                                                               jobjectArray jnames, jintArray jparams)
{
    jsize n = (*env)->GetArrayLength(env, jpcm);
    if (n < 1 || n > (jsize)LFP_SLOTS) return NULL;
    lfp_input_t in[LFP_SLOTS]; char names[LFP_SLOTS][16]; jshort *pcm[LFP_SLOTS]; jshortArray arr[LFP_SLOTS];
    jint *p = (*env)->GetIntArrayElements(env, jparams, NULL);
    for (jsize i = 0; i < n; i++) {
        arr[i] = (jshortArray)(*env)->GetObjectArrayElement(env, jpcm, i);
        pcm[i] = (*env)->GetShortArrayElements(env, arr[i], NULL);
        jstring s = (jstring)(*env)->GetObjectArrayElement(env, jnames, i);
        const char *c = (*env)->GetStringUTFChars(env, s, NULL);
        strncpy(names[i], c, 12); names[i][12] = 0; (*env)->ReleaseStringUTFChars(env, s, c);
        const jint *q = p + 9 * i;
        in[i] = (lfp_input_t){ names[i], pcm[i], (uint32_t)(*env)->GetArrayLength(env, arr[i]),
                               (uint32_t)q[4], q[5] < 0 ? LFP_NO_LOOP : (uint32_t)q[5], (uint8_t)q[0], (uint8_t)q[1], (uint8_t)q[2], (uint8_t)q[3], (uint8_t)q[6], (uint8_t)q[7], (uint8_t)q[8] };
    }
    const char *pn = (*env)->GetStringUTFChars(env, jname, NULL);
    uint32_t bytes = 0; uint8_t *img = lfp_build_ex(pn, (unsigned)scale, (unsigned)scaleRoot, in, (unsigned)n, &bytes);
    (*env)->ReleaseStringUTFChars(env, jname, pn);
    for (jsize i = 0; i < n; i++) (*env)->ReleaseShortArrayElements(env, arr[i], pcm[i], JNI_ABORT);
    (*env)->ReleaseIntArrayElements(env, jparams, p, JNI_ABORT);
    if (!img) return NULL;
    jbyteArray out = (*env)->NewByteArray(env, (jsize)bytes);
    (*env)->SetByteArrayRegion(env, out, 0, (jsize)bytes, (const jbyte *)img);
    free(img); return out;
}
/* What the device will play: encode with the 2 bit codec and decode again (bit exact with the firmware decoder). */
JNIEXPORT jshortArray JNICALL Java_com_ferluht_lofi_Lofi_roundTrip(JNIEnv *env, jclass cls, jshortArray jin)
{
    jsize n = (*env)->GetArrayLength(env, jin); if (n < 1) return (*env)->NewShortArray(env, 0);
    jshort *in = (*env)->GetShortArrayElements(env, jin, NULL);
    int16_t *rec = calloc((size_t)n + LOFI_SUPER, 2); uint8_t *e = lofi_encode(in, (unsigned)n, rec);
    (*env)->ReleaseShortArrayElements(env, jin, in, JNI_ABORT);
    jshortArray out = (*env)->NewShortArray(env, n);
    if (e && rec) (*env)->SetShortArrayRegion(env, out, 0, n, rec);
    free(e); free(rec); return out;
}
JNIEXPORT jint JNICALL Java_com_ferluht_lofi_Lofi_encodedBytes(JNIEnv *env, jclass cls, jint frames) { return (jint)lofi_encoded_bytes((unsigned)frames); }
