/*
  ==============================================================================

    DirectSource.h
    Created: 27 Apr 2017 11:14:15am
    Author:  David Bau

  ==============================================================================
*/

#ifndef DirectSource_H_INCLUDED
#define DirectSource_H_INCLUDED

#include "SOFAData.h"
#include "fftw3.h"
#include "ParameterStruct.h"
#include "Delayline.h"

#define RE 0
#define IM 1


class DirectSource{

public:
    DirectSource();
    ~DirectSource();
    
    void process(const float* inBuffer, float* outBuffer_L, float* outBuffer_R, int numSamples, parameterStruct params);
    void prepareToPlay();
    int getComplexLength();
    
    int initWithSofaData(SOFAData* sD, int _sampleRate);
    
private:
    SOFAData* sofaData;
    int sampleRate;
    
    int firLength;
    int fftLength;
    int complexLength;
    float fftSampleScale;
    
    fftwf_plan forward;
    fftwf_plan inverse_L;
    fftwf_plan inverse_R;
    
    float* fftInputBuffer;
    fftwf_complex* complexBuffer;
    fftwf_complex* src;
    float* fftOutputBuffer_L;
    float* fftOutputBuffer_R;
    
    float* inputBuffer;
    float* lastInputBuffer;
    float* outputBuffer_L;
    float* outputBuffer_R;
    float* weightingCurve;
    
    int fifoIndex;
    
    fftwf_complex* previousHRTF_l;
    fftwf_complex* previousHRTF_r;
    float previousAzimuth;
    float previousElevation;
    
    float calculateNFAngleOffset(float angle, float r, float earPosition);
    
    void releaseResources();
    
    Delayline ITDdelayL;
    Delayline ITDdelayR;
    
    float delayL_ms;
    float delayR_ms;

    
};



#endif  // DirectSource_H_INCLUDED
