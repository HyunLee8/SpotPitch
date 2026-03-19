#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <atomic>
#include <math.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <rubberband/RubberBandStretcher.h>
#include "fishhook.h"
#include "reverb.h"
#include "effects.h"

using namespace RubberBand;

#define SOCKET_PATH  "/tmp/spotpitch.sock"
#define HTTP_PORT    7337
#define CHANNELS     2
#define MAX_FRAMES   4096

static void enable_flush_to_zero() {
    #if defined(__arm__) || defined(__arm64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1 << 24);
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
    #endif
}

static AudioDeviceIOProc    spotify_io_proc = nullptr;
static RubberBandStretcher* stretcher       = nullptr;
static Freeverb             reverb;
static Effects              effects;

static std::atomic<double> target_pitch{1.0};
static std::atomic<double> current_pitch{1.0};
static std::atomic<double> target_speed{1.0};
static std::atomic<float>  reverb_room{0.5f};
static std::atomic<float>  reverb_wet{0.3f};
static std::atomic<float>  reverb_predelay{20.0f};
static std::atomic<bool>   reverb_enabled{false};
static std::atomic<bool>   reverb_dirty{false};
static std::atomic<bool>   reset_pending{false};

static std::atomic<bool>   eq_enabled{false};
static std::atomic<float>  eq_bass{0.0f};
static std::atomic<float>  eq_mid{0.0f};
static std::atomic<float>  eq_treble{0.0f};
static std::atomic<bool>   wide_enabled{false};
static std::atomic<float>  wide_amount{0.5f};
static std::atomic<bool>   vinyl_enabled{false};
static std::atomic<float>  vinyl_amount{0.5f};
static std::atomic<bool>   chorus_enabled{false};
static std::atomic<float>  chorus_amount{0.3f};
static std::atomic<bool>   effects_dirty{false};

static inline float soft_clip(float x) {
    if (x >  1.0f) return  1.0f - expf(-(x - 1.0f));
    if (x < -1.0f) return -1.0f + expf((x + 1.0f));
    return x;
}

static void handle_cmd(const char* line) {
    if      (strncmp(line,"pitch:",6)==0)           { target_pitch.store(atof(line+6)); }
    else if (strncmp(line,"speed:",6)==0)           { double v=atof(line+6); if(v>1.0)v=1.0; if(v<0.1)v=0.1; target_speed.store(v); }
    else if (strncmp(line,"reverb_enabled:",15)==0) { reverb_enabled.store(atoi(line+15)==1); reverb_dirty.store(true); }
    else if (strncmp(line,"reverb_room:",12)==0)    { reverb_room.store(atof(line+12)); reverb_dirty.store(true); }
    else if (strncmp(line,"reverb_wet:",11)==0)     { reverb_wet.store(atof(line+11)); reverb_dirty.store(true); }
    else if (strncmp(line,"reverb_predelay:",16)==0){ reverb_predelay.store(atof(line+16)); reverb_dirty.store(true); }
    else if (strncmp(line,"eq_enabled:",11)==0)     { eq_enabled.store(atoi(line+11)==1); effects_dirty.store(true); }
    else if (strncmp(line,"eq_bass:",8)==0)         { eq_bass.store(atof(line+8)); effects_dirty.store(true); }
    else if (strncmp(line,"eq_mid:",7)==0)          { eq_mid.store(atof(line+7)); effects_dirty.store(true); }
    else if (strncmp(line,"eq_treble:",10)==0)      { eq_treble.store(atof(line+10)); effects_dirty.store(true); }
    else if (strncmp(line,"wide_enabled:",13)==0)   { wide_enabled.store(atoi(line+13)==1); effects_dirty.store(true); }
    else if (strncmp(line,"wide_amount:",12)==0)    { wide_amount.store(atof(line+12)); effects_dirty.store(true); }
    else if (strncmp(line,"vinyl_enabled:",14)==0)  { vinyl_enabled.store(atoi(line+14)==1); effects_dirty.store(true); }
    else if (strncmp(line,"vinyl_amount:",13)==0)   { vinyl_amount.store(atof(line+13)); effects_dirty.store(true); }
    else if (strncmp(line,"chorus_enabled:",15)==0) { chorus_enabled.store(atoi(line+15)==1); effects_dirty.store(true); }
    else if (strncmp(line,"chorus_amount:",14)==0)  { chorus_amount.store(atof(line+14)); effects_dirty.store(true); }
}

static void* track_monitor_thread(void*) {
    char last_track[512] = "";
    while (true) {
        usleep(400000);
        FILE* pipe = popen("osascript -e 'tell application \"Spotify\" to get spotify url of current track' 2>/dev/null", "r");
        if (!pipe) continue;
        char track[512] = {};
        fgets(track, sizeof(track), pipe);
        pclose(pipe);
        int len = strlen(track);
        while (len > 0 && (track[len-1]=='\n'||track[len-1]=='\r')) track[--len]='\0';
        if (strlen(track)>0 && strcmp(track,last_track)!=0) {
            if (strlen(last_track)>0) reset_pending.store(true);
            strncpy(last_track, track, sizeof(last_track)-1);
        }
    }
    return nullptr;
}

static void* ipc_thread(void*) {
    unlink(SOCKET_PATH);
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return nullptr;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return nullptr;
    listen(server_fd, 5);

    FILE* f = fopen("/tmp/spotpitch.log", "a");
    if (f) { fprintf(f, "SpotPitch: IPC ready\n"); fclose(f); }

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;
        char buf[4096] = {};
        ssize_t n = read(client_fd, buf, sizeof(buf)-1);
        if (n > 0) {
            buf[n] = '\0';
            // Handle multiple newline-separated commands
            char* line = strtok(buf, "\n");
            while (line) {
                // Strip carriage returns
                int l = strlen(line);
                while (l>0 && (line[l-1]=='\r'||line[l-1]==' ')) line[--l]='\0';
                if (l>0) handle_cmd(line);
                line = strtok(nullptr, "\n");
            }
            write(client_fd, "ok\n", 3);
        }
        close(client_fd);
    }
    return nullptr;
}

static void* http_thread(void*) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return nullptr;
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(HTTP_PORT);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return nullptr;
    listen(server_fd, 5);

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;
        char req[512] = {};
        read(client_fd, req, sizeof(req)-1);
        char body[32];
        snprintf(body, sizeof(body), "%.4f", target_speed.load());
        char response[256];
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n\r\n%s",
            strlen(body), body);
        write(client_fd, response, strlen(response));
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
    Float64 rate = 48000.0; UInt32 size = sizeof(rate);
    AudioObjectGetPropertyData(device, &addr, 0, nullptr, &size, &rate);
    return rate;
}

OSStatus our_io_proc(
    AudioObjectID device, const AudioTimeStamp* now,
    const AudioBufferList* inputData, const AudioTimeStamp* inputTime,
    AudioBufferList* outputData, const AudioTimeStamp* outputTime, void* clientData)
{
    static bool ftz_set = false;
    if (!ftz_set) { enable_flush_to_zero(); ftz_set = true; }

    OSStatus result = spotify_io_proc(device, now, inputData, inputTime,
                                      outputData, outputTime, clientData);

    if (!stretcher || !outputData || outputData->mNumberBuffers == 0) return result;

    if (reset_pending.load()) {
        reset_pending.store(false);
        double sr = 48000.0;
        delete stretcher;
        stretcher = new RubberBandStretcher((size_t)sr, CHANNELS,
            RubberBandStretcher::OptionProcessRealTime |
            RubberBandStretcher::OptionPitchHighConsistency,
            1.0, current_pitch.load());
        stretcher->setMaxProcessSize(MAX_FRAMES);
        float silence[MAX_FRAMES] = {};
        float* prime_ch[2] = { silence, silence };
        stretcher->process(prime_ch, MAX_FRAMES, false);
    }

    double new_pitch = target_pitch.load();
    if (new_pitch != current_pitch.load()) {
        stretcher->setPitchScale(new_pitch);
        current_pitch.store(new_pitch);
    }

    static double current_speed = 1.0;
    double new_speed = target_speed.load();
    if (new_speed != current_speed) {
        stretcher->setTimeRatio(1.0 / new_speed);
        current_speed = new_speed;
    }

    if (reverb_dirty.load()) {
        reverb.setEnabled(reverb_enabled.load());
        reverb.setRoomSize(reverb_room.load());
        reverb.setWet(reverb_wet.load());
        reverb.setPreDelay(reverb_predelay.load());
        reverb_dirty.store(false);
    }

    if (effects_dirty.load()) {
        effects.eq_enabled     = eq_enabled.load();
        effects.wide_enabled   = wide_enabled.load();
        effects.vinyl_enabled  = vinyl_enabled.load();
        effects.chorus_enabled = chorus_enabled.load();
        effects.setEQ(eq_bass.load(), eq_mid.load(), eq_treble.load());
        effects.setWide(wide_amount.load());
        effects.setVinyl(vinyl_amount.load());
        effects.setChorus(chorus_amount.load());
        effects_dirty.store(false);
    }

    float* interleaved = (float*)outputData->mBuffers[0].mData;
    UInt32 frameCount  = outputData->mBuffers[0].mDataByteSize / (sizeof(float) * CHANNELS);
    if (frameCount == 0 || frameCount > MAX_FRAMES) return result;

    static float left[MAX_FRAMES], right[MAX_FRAMES];
    float* channels[2] = { left, right };

    for (UInt32 i = 0; i < frameCount; i++) {
        left[i]  = interleaved[i*2];
        right[i] = interleaved[i*2+1];
    }

    stretcher->process(channels, frameCount, false);

    int available = stretcher->available();
    if (available <= 0) { memset(interleaved, 0, outputData->mBuffers[0].mDataByteSize); return result; }

    UInt32 toRead = (UInt32)available < frameCount ? (UInt32)available : frameCount;
    stretcher->retrieve(channels, toRead);

    if (toRead < frameCount) {
        memset(left  + toRead, 0, (frameCount - toRead) * sizeof(float));
        memset(right + toRead, 0, (frameCount - toRead) * sizeof(float));
    }

    reverb.process(left, right, frameCount);
    effects.process(left, right, frameCount);

    for (UInt32 i = 0; i < frameCount; i++) {
        interleaved[i*2]   = soft_clip(left[i]);
        interleaved[i*2+1] = soft_clip(right[i]);
    }

    return result;
}

typedef OSStatus (*CreateIOProcFunc)(AudioObjectID, AudioDeviceIOProc, void*, AudioDeviceIOProcID*);
static CreateIOProcFunc orig_CreateIOProc = nullptr;

OSStatus hooked_CreateIOProc(AudioObjectID device, AudioDeviceIOProc proc,
                              void* clientData, AudioDeviceIOProcID* outID) {
    spotify_io_proc = proc;
    double sampleRate = getDeviceSampleRate(device);

    if (stretcher) { delete stretcher; stretcher = nullptr; }
    stretcher = new RubberBandStretcher((size_t)sampleRate, CHANNELS,
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionPitchHighConsistency,
        1.0, current_pitch.load());
    stretcher->setMaxProcessSize(MAX_FRAMES);

    FILE* f = fopen("/tmp/spotpitch.log", "a");
    if (f) { fprintf(f, "SpotPitch: ready PID=%d sr=%.0f\n", getpid(), sampleRate); fclose(f); }

    pthread_t t1, t2, t3;
    pthread_create(&t1, nullptr, ipc_thread,           nullptr); pthread_detach(t1);
    pthread_create(&t2, nullptr, http_thread,          nullptr); pthread_detach(t2);
    pthread_create(&t3, nullptr, track_monitor_thread, nullptr); pthread_detach(t3);

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
