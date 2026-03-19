#pragma once
#include <math.h>
#include <string.h>

#define EFF_SR 48000.0f

// ── Biquad Filter ─────────────────────────────────────────────────────────────
struct Biquad {
    float b0,b1,b2,a1,a2;
    float x1=0,x2=0,y1=0,y2=0;

    void setLowShelf(float freq, float gainDb) {
        float A  = powf(10.0f, gainDb/40.0f);
        float w0 = 2.0f*3.14159f*freq/EFF_SR;
        float cosw = cosf(w0), sinw = sinf(w0);
        float S = 1.0f;
        float alpha = sinw/2.0f * sqrtf((A+1.0f/A)*(1.0f/S-1.0f)+2.0f);
        float b0_ = A*((A+1)-(A-1)*cosw+2*sqrtf(A)*alpha);
        float b1_ = 2*A*((A-1)-(A+1)*cosw);
        float b2_ = A*((A+1)-(A-1)*cosw-2*sqrtf(A)*alpha);
        float a0_ = (A+1)+(A-1)*cosw+2*sqrtf(A)*alpha;
        float a1_ = -2*((A-1)+(A+1)*cosw);
        float a2_ = (A+1)+(A-1)*cosw-2*sqrtf(A)*alpha;
        b0=b0_/a0_; b1=b1_/a0_; b2=b2_/a0_;
        a1=a1_/a0_; a2=a2_/a0_;
    }

    void setHighShelf(float freq, float gainDb) {
        float A  = powf(10.0f, gainDb/40.0f);
        float w0 = 2.0f*3.14159f*freq/EFF_SR;
        float cosw = cosf(w0), sinw = sinf(w0);
        float S = 1.0f;
        float alpha = sinw/2.0f * sqrtf((A+1.0f/A)*(1.0f/S-1.0f)+2.0f);
        float b0_ = A*((A+1)+(A-1)*cosw+2*sqrtf(A)*alpha);
        float b1_ = -2*A*((A-1)+(A+1)*cosw);
        float b2_ = A*((A+1)+(A-1)*cosw-2*sqrtf(A)*alpha);
        float a0_ = (A+1)-(A-1)*cosw+2*sqrtf(A)*alpha;
        float a1_ = 2*((A-1)-(A+1)*cosw);
        float a2_ = (A+1)-(A-1)*cosw-2*sqrtf(A)*alpha;
        b0=b0_/a0_; b1=b1_/a0_; b2=b2_/a0_;
        a1=a1_/a0_; a2=a2_/a0_;
    }

    void setPeaking(float freq, float Q, float gainDb) {
        float A  = powf(10.0f, gainDb/40.0f);
        float w0 = 2.0f*3.14159f*freq/EFF_SR;
        float alpha = sinf(w0)/(2.0f*Q);
        float cosw = cosf(w0);
        float a0_ = 1+alpha/A;
        b0=(1+alpha*A)/a0_; b1=(-2*cosw)/a0_; b2=(1-alpha*A)/a0_;
        a1=(-2*cosw)/a0_;   a2=(1-alpha/A)/a0_;
    }

    void setLowPass(float freq, float Q=0.707f) {
        float w0 = 2.0f*3.14159f*freq/EFF_SR;
        float alpha = sinf(w0)/(2.0f*Q);
        float cosw = cosf(w0);
        float a0_ = 1+alpha;
        b0=(1-cosw)/2/a0_; b1=(1-cosw)/a0_; b2=(1-cosw)/2/a0_;
        a1=(-2*cosw)/a0_;  a2=(1-alpha)/a0_;
    }

    float process(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x; y2=y1; y1=y;
        return y;
    }

    void reset() { x1=x2=y1=y2=0; }
};

// ── Smooth Value ──────────────────────────────────────────────────────────────
struct SmoothVal {
    float val, target;
    float coeff;
    SmoothVal(float v=0, float ms=30.0f) {
        val=target=v;
        coeff=expf(-1.0f/(EFF_SR*ms/1000.0f));
    }
    void set(float v){ target=v; }
    float next(){ val=val*coeff+target*(1.0f-coeff); return val; }
};

// ── Delay ─────────────────────────────────────────────────────────────────────
struct SimpleDelay {
    float* buf; int size, pos;
    SimpleDelay(): buf(nullptr),size(0),pos(0){}
    void init(int n){ size=n; buf=new float[n](); pos=0; }
    void write(float v){ buf[pos]=v; if(++pos>=size) pos=0; }
    float read(int d){ int i=pos-d; if(i<0) i+=size; return buf[i]; }
};

// ── Effects Chain ─────────────────────────────────────────────────────────────
class Effects {
public:
    // EQ
    bool  eq_enabled = false;
    Biquad bass_L, bass_R;
    Biquad mid_L,  mid_R;
    Biquad treble_L, treble_R;
    SmoothVal smooth_bass{0}, smooth_mid{0}, smooth_treble{0};

    // Stereo Widener
    bool  wide_enabled = false;
    SmoothVal smooth_wide{0};

    // Vinyl
    bool  vinyl_enabled = false;
    float vinyl_phase   = 0;
    float crackle_state = 0;
    Biquad vinyl_lp_L, vinyl_lp_R;
    SmoothVal smooth_vinyl{0};

    // Chorus
    bool  chorus_enabled = false;
    SimpleDelay chorus_dL, chorus_dR;
    float chorus_lfo1 = 0, chorus_lfo2 = 0;
    SmoothVal smooth_chorus{0};

    Effects() {
        // Init EQ defaults
        bass_L.setLowShelf(200.0f, 0);    bass_R.setLowShelf(200.0f, 0);
        mid_L.setPeaking(1000.0f,1.0f,0); mid_R.setPeaking(1000.0f,1.0f,0);
        treble_L.setHighShelf(8000.0f,0); treble_R.setHighShelf(8000.0f,0);

        // Vinyl LP
        vinyl_lp_L.setLowPass(12000.0f); vinyl_lp_R.setLowPass(12000.0f);

        // Chorus delay lines (50ms max)
        chorus_dL.init((int)(EFF_SR*0.05f));
        chorus_dR.init((int)(EFF_SR*0.05f));
    }

    void setEQ(float bassDb, float midDb, float trebleDb) {
        smooth_bass.set(bassDb);
        smooth_mid.set(midDb);
        smooth_treble.set(trebleDb);
    }

    void setWide(float amount) { smooth_wide.set(amount); }
    void setVinyl(float amount) { smooth_vinyl.set(amount); }
    void setChorus(float amount) { smooth_chorus.set(amount); }

    void process(float* left, float* right, int frames) {

        for (int i = 0; i < frames; i++) {
            float L = left[i];
            float R = right[i];

            // ── EQ ──────────────────────────────────────────────
            if (eq_enabled) {
                float b = smooth_bass.next();
                float m = smooth_mid.next();
                float t = smooth_treble.next();

                bass_L.setLowShelf(200.0f, b);   bass_R.setLowShelf(200.0f, b);
                mid_L.setPeaking(1000.0f,1.0f,m); mid_R.setPeaking(1000.0f,1.0f,m);
                treble_L.setHighShelf(8000.0f,t); treble_R.setHighShelf(8000.0f,t);

                L = treble_L.process(mid_L.process(bass_L.process(L)));
                R = treble_R.process(mid_R.process(bass_R.process(R)));
            }

            // ── STEREO WIDENER ───────────────────────────────────
            if (wide_enabled) {
                float w = smooth_wide.next();
                float mid  = (L + R) * 0.5f;
                float side = (L - R) * 0.5f * (1.0f + w * 2.0f);
                L = mid + side;
                R = mid - side;
            }

            // ── CHORUS ──────────────────────────────────────────
            if (chorus_enabled) {
                float amount = smooth_chorus.next();
                chorus_lfo1 += 0.00040f; if(chorus_lfo1>1) chorus_lfo1-=1;
                chorus_lfo2 += 0.00031f; if(chorus_lfo2>1) chorus_lfo2-=1;

                float mod1 = (sinf(chorus_lfo1*2*3.14159f)+1)*0.5f;
                float mod2 = (sinf(chorus_lfo2*2*3.14159f)+1)*0.5f;

                int d1 = 200 + (int)(mod1*800);
                int d2 = 200 + (int)(mod2*800);

                chorus_dL.write(L);
                chorus_dR.write(R);

                float wetL = chorus_dL.read(d1) * amount;
                float wetR = chorus_dR.read(d2) * amount;

                L = L*(1.0f-amount*0.3f) + wetL;
                R = R*(1.0f-amount*0.3f) + wetR;
            }

            // ── VINYL ────────────────────────────────────────────
            if (vinyl_enabled) {
                float amount = smooth_vinyl.next();

                // Subtle wow (pitch wobble)
                vinyl_phase += 0.000052f;
                if(vinyl_phase > 1) vinyl_phase -= 1;
                float wow = sinf(vinyl_phase*2*3.14159f) * amount * 0.003f;

                // Crackle (sparse noise)
                crackle_state *= 0.98f;
                if ((float)rand()/RAND_MAX < 0.001f * amount) {
                    crackle_state = ((float)rand()/RAND_MAX * 2 - 1) * amount * 0.15f;
                }

                // High frequency roll-off
                L = vinyl_lp_L.process(L) + crackle_state;
                R = vinyl_lp_R.process(R) + crackle_state * 0.7f;

                // Apply wow as volume modulation
                L *= (1.0f + wow);
                R *= (1.0f - wow);
            }

            left[i]  = L;
            right[i] = R;
        }
    }
};
