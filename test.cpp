#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stdlib.h>
#include <atomic>
#include <math.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <rubberband/RubberBandStretcher.h>
#include "fishhook.h"
#include "reverb.h"

using namespace RubberBand;

#define SOCKET_PATH "/tmp/spotpitch.sock"
#define CHANNELS 2
#define MAX_FRAMES 4096

// Flush denormals to zero — prevents CPU spikes
#include <fenv.h>
static void enable_flush_to_zero() {
    #if defined(__arm__) || defined(__arm64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1 << 24); // FZ bit
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
    #endif
}

static AudioDeviceIOProc spotify_io_proc = nullptr;
static RubberBandStretcher* stretcher = nullptr;
static Freeverb reverb;

static std::atomic<double> target_pitch{1.0};
static std::atomic<double> current_pitch{1.0};
static std::atomic<double> target_speed{1.0};
static std::atomic<double> current_speed{1.0};
static std::atomic<float> reverb_room{0.5f};
static std::atomic<float> reverb_wet{0.3f};
static std::atomic<bool> reverb_enabled{false};
static std::atomic<bool> reverb_dirty{false};

// Soft clip to prevent distortion — sounds musical not harsh
static inline float soft_clip(float x) {
    if (x > 1.0f)  return 1.0f - expf(-(x - 1.0f));
    if (x < -1.0f) return -1.0f + expf((x + 1.0f));
    return x;
}

static void* ipc_thread(void*) {
    unlink(SOCKET_PATH);
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return nullptr;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return nullptr;
    listen(server_fd, 5);

    FILE* f = fopen("/tmp/spotpitch.log", "a");
    if (f) { fprintf(f, "SpotPitch: IPC ready PID=%d\n", getpid()); fclose(f); }

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;
        char buf[256] = {};
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            if (strncmp(buf, "pitch:", 6) == 0) {
                target_pitch.store(atof(buf + 6));
                write(client_fd, "ok\n", 3);
            } else if (strncmp(buf, "speed:", 6) == 0) {
                target_speed.store(atof(buf + 6));
                write(client_fd, "ok\n", 3);
            } else if (strncmp(buf, "reverb_enabled:", 15) == 0) {
                reverb_enabled.store(atoi(buf + 15) == 1);
                reverb_dirty.store(true);
                write(client_fd, "ok\n", 3);
            } else if (strncmp(buf, "reverb_room:", 12) == 0) {
                reverb_room.store(atof(buf + 12));
                reverb_dirty.store(true);
                write(client_fd, "ok\n", 3);
            } else if (strncmp(buf, "reverb_wet:", 11) == 0) {
                reverb_wet.store(atof(buf + 11));
                reverb_dirty.store(true);
                write(client_fd, "ok\n", 3);
            }
        }
        close(client_fd);
    }
    return nullptr;
}

static double getDeviceSampleRate(AudioObjectID device) {
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    Float64 rate = 48000.0;
    UInt32 size = sizeof(rate);
    AudioObjectGetPropertyData(device, &addr, 0, nullptr, &size, &rate);
    return rate;
}

OSStatus our_io_proc(
    AudioObjectID device,
    const AudioTimeStamp* now,
    const AudioBufferList* inputData,
    const AudioTimeStamp* inputTime,
    AudioBufferList* outputData,
    const AudioTimeStamp* outputTime,
    void* clientData
) {
    // Enable flush to zero on this thread
    static bool ftz_set = false;
    if (!ftz_set) { enable_flush_to_zero(); ftz_set = true; }

    OSStatus result = spotify_io_proc(
        device, now, inputData, inputTime,
        outputData, outputTime, clientData
    );

    if (!stretcher || !outputData || outputData->mNumberBuffers == 0) return result;

    // Apply pitch changes atomically
    double new_pitch = target_pitch.load();
    if (new_pitch != current_pitch.load()) {
        stretcher->setPitchScale(new_pitch);
        current_pitch.store(new_pitch);
    }

    // Apply speed changes atomically
    double new_speed = target_speed.load();
    if (new_speed != current_speed.load()) {
        stretcher->setTimeRatio(1.0 / new_speed);
        current_speed.store(new_speed);
    }

    // Apply reverb settings if dirty
    if (reverb_dirty.load()) {
        reverb.setEnabled(reverb_enabled.load());
        reverb.setRoomSize(reverb_room.load());
        reverb.setWet(reverb_wet.load());
        reverb_dirty.store(false);
    }

    float* interleaved = (float*)outputData->mBuffers[0].mData;
    UInt32 frameCount = outputData->mBuffers[0].mDataByteSize / (sizeof(float) * CHANNELS);

    if (frameCount == 0 || frameCount > MAX_FRAMES) return result;

    // Static buffers — no heap allocation on audio thread
    static float left[MAX_FRAMES];
    static float right[MAX_FRAMES];
    float* channels[2] = { left, right };

    // Deinterleave
    for (UInt32 i = 0; i < frameCount; i++) {
        left[i]  = interleaved[i * 2];
        right[i] = interleaved[i * 2 + 1];
    }

    stretcher->process(channels, frameCount, false);

    int available = stretcher->available();
    if (available <= 0) {
        // No output yet — fill with silence to avoid garbage
        memset(interleaved, 0, outputData->mBuffers[0].mDataByteSize);
        return result;
    }

    UInt32 toRead = (UInt32)available < frameCount ? (UInt32)available : frameCount;
    stretcher->retrieve(channels, toRead);

    // If we got fewer frames than expected, zero the rest
    if (toRead < frameCount) {
        memset(left + toRead, 0, (frameCount - toRead) * sizeof(float));
        memset(right + toRead, 0, (frameCount - toRead) * sizeof(float));
    }

    // Apply reverb
    reverb.process(left, right, frameCount);

    // Reinterleave with soft clipping to prevent distortion
    for (UInt32 i = 0; i < frameCount; i++) {
        interleaved[i * 2]     = soft_clip(left[i]);
        interleaved[i * 2 + 1] = soft_clip(right[i]);
    }

    return result;
}

typedef OSStatus (*CreateIOProcFunc)(
    AudioObjectID, AudioDeviceIOProc, void*, AudioDeviceIOProcID*
);
static CreateIOProcFunc orig_CreateIOProc = nullptr;

OSStatus hooked_CreateIOProc(
    AudioObjectID device, AudioDeviceIOProc proc,
    void* clientData, AudioDeviceIOProcID* outID
) {
    spotify_io_proc = proc;
    double sampleRate = getDeviceSampleRate(device);

    stretcher = new RubberBandStretcher(
        (size_t)sampleRate, CHANNELS,
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionPitchHighConsistency,
        1.0, 1.0
    );
    stretcher->setMaxProcessSize(MAX_FRAMES);

    FILE* f = fopen("/tmp/spotpitch.log", "a");
    if (f) { fprintf(f, "SpotPitch: ready PID=%d sampleRate=%.0f\n", getpid(), sampleRate); fclose(f); }

    pthread_t thread;
    pthread_create(&thread, nullptr, ipc_thread, nullptr);
    pthread_detach(thread);

    return orig_CreateIOProc(device, our_io_proc, clientData, outID);
}

__attribute__((constructor))
void init() {
    rebind_symbols((struct rebinding[1]){{
        "AudioDeviceCreateIOProcID",
        (void*)hooked_CreateIOProc,
        (void**)&orig_CreateIOProc
    }}, 1);
}
