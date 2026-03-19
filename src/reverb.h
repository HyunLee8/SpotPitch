#pragma once
#include <math.h>
#include <string.h>

#define NUM_COMBS 8
#define NUM_ALLPASS 4
#define FIXED_GAIN 0.015f
#define SCALE_WET 3.0f
#define SCALE_DAMP 0.4f
#define SCALE_ROOM 0.28f
#define OFFSET_ROOM 0.7f
#define STEREO_SPREAD 23

static const int comb_tuning_L[NUM_COMBS]     = {1116,1188,1277,1356,1422,1491,1557,1617};
static const int comb_tuning_R[NUM_COMBS]     = {1116+STEREO_SPREAD,1188+STEREO_SPREAD,1277+STEREO_SPREAD,1356+STEREO_SPREAD,1422+STEREO_SPREAD,1491+STEREO_SPREAD,1557+STEREO_SPREAD,1617+STEREO_SPREAD};
static const int allpass_tuning_L[NUM_ALLPASS] = {556,441,341,225};
static const int allpass_tuning_R[NUM_ALLPASS] = {556+STEREO_SPREAD,441+STEREO_SPREAD,341+STEREO_SPREAD,225+STEREO_SPREAD};

struct SmoothParam {
    float current, target, coeff;
    SmoothParam(float val=0, float sr=48000.0f, float ms=50.0f) {
        current = target = val;
        coeff = expf(-1.0f / (sr * ms / 1000.0f));
    }
    void set(float v) { target = v; }
    float next() { current = current*coeff + target*(1.0f-coeff); return current; }
};

class CombFilter {
    float* buf; int size, idx;
    float feedback, damp1, damp2, store;
public:
    CombFilter(): buf(nullptr),size(0),idx(0),feedback(0),damp1(0),damp2(1),store(0){}
    void init(int n){ size=n; buf=new float[n](); idx=0; store=0; }
    void setFeedback(float v){ feedback=v; }
    void setDamp(float v){ damp1=v; damp2=1.0f-v; }
    float process(float in){
        float out=buf[idx];
        store=(out*damp2)+(store*damp1);
        buf[idx]=in+(store*feedback);
        if(++idx>=size) idx=0;
        return out;
    }
};

class AllpassFilter {
    float* buf; int size, idx;
public:
    AllpassFilter(): buf(nullptr),size(0),idx(0){}
    void init(int n){ size=n; buf=new float[n](); idx=0; }
    float process(float in){
        float out=buf[idx];
        buf[idx]=in+out*0.5f;
        if(++idx>=size) idx=0;
        return out-in;
    }
};

class Freeverb {
    CombFilter    combL[NUM_COMBS],    combR[NUM_COMBS];
    AllpassFilter apL[NUM_ALLPASS],    apR[NUM_ALLPASS];

    // Pre-delay buffers (max 100ms at 48000)
    float* pre_buf_L;
    float* pre_buf_R;
    int    pre_size;
    int    pre_pos;
    int    pre_delay_samples;

    SmoothParam smooth_feedback, smooth_damp, smooth_wet1, smooth_wet2, smooth_dry;

    float width   = 1.0f;
    bool  enabled = false;

public:
    Freeverb() {
        float sr = 48000.0f;
        float scale = sr / 44100.0f;

        for(int i=0;i<NUM_COMBS;i++){
            combL[i].init((int)(comb_tuning_L[i]*scale));
            combR[i].init((int)(comb_tuning_R[i]*scale));
        }
        for(int i=0;i<NUM_ALLPASS;i++){
            apL[i].init((int)(allpass_tuning_L[i]*scale));
            apR[i].init((int)(allpass_tuning_R[i]*scale));
        }

        // Pre-delay: 100ms max
        pre_size = (int)(sr * 0.1f);
        pre_buf_L = new float[pre_size]();
        pre_buf_R = new float[pre_size]();
        pre_pos = 0;
        pre_delay_samples = (int)(sr * 0.02f); // default 20ms

        smooth_feedback = SmoothParam(0.5f*SCALE_ROOM+OFFSET_ROOM, sr, 60.0f);
        smooth_damp     = SmoothParam(0.5f*SCALE_DAMP,             sr, 60.0f);
        smooth_wet1     = SmoothParam(0.3f*SCALE_WET*0.75f,        sr, 40.0f);
        smooth_wet2     = SmoothParam(0.0f,                         sr, 40.0f);
        smooth_dry      = SmoothParam(0.7f,                         sr, 40.0f);

        for(int i=0;i<NUM_COMBS;i++){
            combL[i].setFeedback(smooth_feedback.current);
            combR[i].setFeedback(smooth_feedback.current);
            combL[i].setDamp(smooth_damp.current);
            combR[i].setDamp(smooth_damp.current);
        }
    }

    void setEnabled(bool e){ enabled=e; }
    bool isEnabled(){ return enabled; }

    void setRoomSize(float v){
        smooth_feedback.set(v*SCALE_ROOM+OFFSET_ROOM);
    }

    void setWet(float v){
        float wet = v * SCALE_WET;
        smooth_wet1.set(wet*(width/2.0f+0.5f));
        smooth_wet2.set(wet*((1.0f-width)/2.0f));
        smooth_dry.set(1.0f - v*0.35f);
    }

    void setDamp(float v){
        smooth_damp.set(v*SCALE_DAMP);
    }

    void setPreDelay(float ms){
        int s = (int)(ms * 48000.0f / 1000.0f);
        if(s >= pre_size) s = pre_size-1;
        if(s < 0) s = 0;
        pre_delay_samples = s;
    }

    void process(float* left, float* right, int frames){
        if(!enabled) return;

        for(int i=0;i<frames;i++){
            float fb   = smooth_feedback.next();
            float damp = smooth_damp.next();
            float wet1 = smooth_wet1.next();
            float wet2 = smooth_wet2.next();
            float dry  = smooth_dry.next();

            for(int c=0;c<NUM_COMBS;c++){
                combL[c].setFeedback(fb); combR[c].setFeedback(fb);
                combL[c].setDamp(damp);   combR[c].setDamp(damp);
            }

            // Pre-delay
            pre_buf_L[pre_pos] = left[i];
            pre_buf_R[pre_pos] = right[i];
            int read_pos = pre_pos - pre_delay_samples;
            if(read_pos < 0) read_pos += pre_size;
            float pdL = pre_delay_samples > 0 ? pre_buf_L[read_pos] : left[i];
            float pdR = pre_delay_samples > 0 ? pre_buf_R[read_pos] : right[i];
            if(++pre_pos >= pre_size) pre_pos = 0;

            // Comb filters
            float inL = pdL * FIXED_GAIN;
            float inR = pdR * FIXED_GAIN;
            float outL=0, outR=0;
            for(int c=0;c<NUM_COMBS;c++){
                outL += combL[c].process(inL);
                outR += combR[c].process(inR);
            }

            // Allpass filters
            for(int a=0;a<NUM_ALLPASS;a++){
                outL = apL[a].process(outL);
                outR = apR[a].process(outR);
            }

            left[i]  = outL*wet1 + outR*wet2 + left[i]*dry;
            right[i] = outR*wet1 + outL*wet2 + right[i]*dry;
        }
    }
};
