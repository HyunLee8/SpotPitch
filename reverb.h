#pragma once

// Freeverb - Jezar at Dreampoint
// Classic all-pass + comb filter reverb

#define NUM_COMBS 8
#define NUM_ALLPASS 4
#define FIXED_GAIN 0.015f
#define SCALE_WET 3.0f
#define SCALE_DAMP 0.4f
#define SCALE_ROOM 0.28f
#define OFFSET_ROOM 0.7f
#define INITIAL_ROOM 0.5f
#define INITIAL_DAMP 0.5f
#define INITIAL_WET 0.3f
#define INITIAL_DRY 0.7f
#define INITIAL_WIDTH 1.0f
#define STEREO_SPREAD 23

static const int comb_tuning_L[NUM_COMBS]    = {1116,1188,1277,1356,1422,1491,1557,1617};
static const int comb_tuning_R[NUM_COMBS]    = {1116+STEREO_SPREAD,1188+STEREO_SPREAD,1277+STEREO_SPREAD,1356+STEREO_SPREAD,1422+STEREO_SPREAD,1491+STEREO_SPREAD,1557+STEREO_SPREAD,1617+STEREO_SPREAD};
static const int allpass_tuning_L[NUM_ALLPASS] = {556,441,341,225};
static const int allpass_tuning_R[NUM_ALLPASS] = {556+STEREO_SPREAD,441+STEREO_SPREAD,341+STEREO_SPREAD,225+STEREO_SPREAD};

class CombFilter {
    float* buf;
    int size, idx;
    float feedback, damp1, damp2, store;
public:
    CombFilter() : buf(nullptr), size(0), idx(0), feedback(0), damp1(0), damp2(0), store(0) {}
    void init(int n) {
        size = n; buf = new float[n](); idx = 0; store = 0;
    }
    void setFeedback(float v) { feedback = v; }
    void setDamp(float v) { damp1 = v; damp2 = 1.0f - v; }
    float process(float input) {
        float out = buf[idx];
        store = (out * damp2) + (store * damp1);
        buf[idx] = input + (store * feedback);
        if (++idx >= size) idx = 0;
        return out;
    }
};

class AllpassFilter {
    float* buf;
    int size, idx;
public:
    AllpassFilter() : buf(nullptr), size(0), idx(0) {}
    void init(int n) { size = n; buf = new float[n](); idx = 0; }
    float process(float input) {
        float out = buf[idx];
        buf[idx] = input + out * 0.5f;
        if (++idx >= size) idx = 0;
        return out - input;
    }
};

class Freeverb {
    CombFilter combL[NUM_COMBS], combR[NUM_COMBS];
    AllpassFilter apL[NUM_ALLPASS], apR[NUM_ALLPASS];
    float wet1, wet2, dry, damp, roomsize, width;
    float wet;
    bool enabled;

    void update() {
        wet1 = wet * (width/2.0f + 0.5f);
        wet2 = wet * ((1.0f - width)/2.0f);
    }

public:
    Freeverb() : enabled(false) {
        // Scale tunings for 48000 sample rate (tunings are for 44100)
        float sr_scale = 48000.0f / 44100.0f;
        for (int i = 0; i < NUM_COMBS; i++) {
            combL[i].init((int)(comb_tuning_L[i] * sr_scale));
            combR[i].init((int)(comb_tuning_R[i] * sr_scale));
        }
        for (int i = 0; i < NUM_ALLPASS; i++) {
            apL[i].init((int)(allpass_tuning_L[i] * sr_scale));
            apR[i].init((int)(allpass_tuning_R[i] * sr_scale));
        }
        setRoomSize(INITIAL_ROOM);
        setDamp(INITIAL_DAMP);
        setWet(INITIAL_WET);
        setDry(INITIAL_DRY);
        setWidth(INITIAL_WIDTH);
    }

    void setEnabled(bool e) { enabled = e; }
    bool isEnabled() { return enabled; }
    void setRoomSize(float v) {
        roomsize = v * SCALE_ROOM + OFFSET_ROOM;
        for (int i = 0; i < NUM_COMBS; i++) {
            combL[i].setFeedback(roomsize);
            combR[i].setFeedback(roomsize);
        }
    }
    void setDamp(float v) {
        damp = v * SCALE_DAMP;
        for (int i = 0; i < NUM_COMBS; i++) {
            combL[i].setDamp(damp);
            combR[i].setDamp(damp);
        }
    }
    void setWet(float v) { wet = v * SCALE_WET; update(); }
    void setDry(float v) { dry = v; }
    void setWidth(float v) { width = v; update(); }

    void process(float* left, float* right, int frames) {
        if (!enabled) return;
        for (int i = 0; i < frames; i++) {
            float inL = left[i] * FIXED_GAIN;
            float inR = right[i] * FIXED_GAIN;
            float outL = 0, outR = 0;

            for (int c = 0; c < NUM_COMBS; c++) {
                outL += combL[c].process(inL);
                outR += combR[c].process(inR);
            }
            for (int a = 0; a < NUM_ALLPASS; a++) {
                outL = apL[a].process(outL);
                outR = apR[a].process(outR);
            }

            left[i]  = outL * wet1 + outR * wet2 + left[i]  * dry;
            right[i] = outR * wet1 + outL * wet2 + right[i] * dry;
        }
    }
};
