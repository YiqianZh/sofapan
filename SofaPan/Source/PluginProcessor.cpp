/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "EarlyReflection.h"

#include "math.h"

//==============================================================================
SofaPanAudioProcessor::SofaPanAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    addParameter(params.azimuthParam = new AudioParameterFloat("azimuth", "Azimuth", 0.f, 1.f, 0.f));
    addParameter(params.bypassParam = new AudioParameterFloat("bypass", "Bypass", 0.f, 1.f, 0.f));
    addParameter(params.elevationParam = new AudioParameterFloat("elevation", "Elevation", 0.f, 1.f, 0.5f));
    addParameter(params.distanceParam = new AudioParameterFloat("distance", "Distance", 0.f, 1.f, 0.5f));
    addParameter(params.mirrorSourceParam = new AudioParameterBool("mirrorSource", "Mirror Source Model", false));
    addParameter(params.testSwitchParam = new AudioParameterBool("test", "Test Switch", false));
    addParameter(params.distanceSimulationParam = new AudioParameterBool("dist_sim", "Distance Simulation", false));
    addParameter(params.nearfieldSimulationParam = new AudioParameterBool("nearfield_sim", "Nearfield Simulation", false));

    
    HRTFs = new SOFAData();

    earlyReflections.resize(4);
    for(int i = 0; i < earlyReflections.size(); i++)
        earlyReflections[i] = NULL;
    
    sampleRate_f = 0;
    
    updateSofaMetadataFlag = false;

    updater = &SofaPathSharedUpdater::instance();
    String connectionID = updater->createConnection();
    connectToPipe(connectionID, 10);
    
    
    
}

SofaPanAudioProcessor::~SofaPanAudioProcessor()
{
    
    delete HRTFs;
    HRTFs = NULL;
    for(int i=0; i < earlyReflections.size(); i++){
        if(earlyReflections[i] != NULL) delete earlyReflections[i];
        earlyReflections[i] = NULL;
    }
    updater->removeConnection(getPipe()->getName());
    
}

//==============================================================================
const String SofaPanAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SofaPanAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SofaPanAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

double SofaPanAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SofaPanAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SofaPanAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SofaPanAudioProcessor::setCurrentProgram (int index)
{
}

const String SofaPanAudioProcessor::getProgramName (int index)
{
    return String();
}

void SofaPanAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
void SofaPanAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    printf("\n prepare to play \n");
    counter = 0;
    
    if(sampleRate != sampleRate_f){
        sampleRate_f = sampleRate;
        if(usingGlobalSofaFile){
            String currentGlobalSofaFile = updater->requestCurrentFilePath();
            if(currentGlobalSofaFile.length() > 1)
                pathToSOFAFile = currentGlobalSofaFile;
        }
        initData(pathToSOFAFile);
    }else{
        directSource.prepareToPlay();
        for(int i = 0; i < numReflections; i++){
            reflections[i].prepareToPlay(sampleRate);
        }
        for(int i=0; i < earlyReflections.size(); i++)
            earlyReflections[i]->prepareToPlay();
    }
    
    estimatedBlockSize = samplesPerBlock;
    reflectionInBuffer = AudioSampleBuffer(1, estimatedBlockSize);
    reverbInBuffer = AudioSampleBuffer(1, estimatedBlockSize);
    reverbOutBuffer = AudioSampleBuffer(2, estimatedBlockSize);
    reflectionInBuffer.clear();
    reverbInBuffer.clear();
    reverbOutBuffer.clear();

    reverb.prepareToPlay((int)sampleRate);
    
}

void SofaPanAudioProcessor::initData(String sofaFile){
    
    printf("\n initalise Data \n ");
    
    pathToSOFAFile = sofaFile;
    
    suspendProcessing(true);
    
    int status = HRTFs->initSofaData(pathToSOFAFile.toUTF8(), (int)sampleRate_f);
        
    metadata_sofafile = HRTFs->getMetadata();
    
    updateSofaMetadataFlag = true;
    
    status += directSource.initWithSofaData(HRTFs, (int)sampleRate_f);
    
    Line<float> wall[4];
    wall[0]= Line<float>(-5.0, 5.0, 5.0, 5.0); //Front
    wall[1]= Line<float>(5.0, 5.0, 5.0, -5.0); //Right
    wall[2]= Line<float>(5.0, -5.0, -5.0, -5.0); //Back
    wall[3]= Line<float>(-5.0, -5.0, -5.0, 5.0); //Left

    for(int i = 0; i < numReflections; i++){
        status += reflections[i].initWithSofaData(HRTFs, (int)sampleRate_f, wall[i], 1);
    }
    
    const float angles[4] = {90, 270, 0, 180}; //right, left, front, back
    for(int i=0; i < earlyReflections.size(); i++){
        if(earlyReflections[i] != NULL) {
            delete earlyReflections[i];
            earlyReflections[i] = NULL;
        }
        earlyReflections[i] = new EarlyReflection(HRTFs->getHRTFforAngle(0.0, angles[i], 1.0), HRTFs->getLengthOfHRIR(), (int)sampleRate_f);
    }
    
    //If an critical error occured during sofa loading, turn this plugin to a brick 
    if(!status) suspendProcessing(false);
}

void SofaPanAudioProcessor::releaseResources()
{
   
    printf("Playback Stop");
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.

}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SofaPanAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SofaPanAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    
    const int numberOfSamples = buffer.getNumSamples();
    
    if (params.bypassParam->get() == true) { return; }
    
    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    //This should actually never happen, BUT it is possible
    if(estimatedBlockSize != numberOfSamples){
        estimatedBlockSize = numberOfSamples;
        reflectionInBuffer.setSize(reflectionInBuffer.getNumChannels(), estimatedBlockSize);
        reverbInBuffer.setSize(reverbInBuffer.getNumChannels(), estimatedBlockSize);
        reverbOutBuffer.setSize(reverbOutBuffer.getNumChannels(), estimatedBlockSize);
        reflectionInBuffer.clear();
        reverbInBuffer.clear();
        reverbOutBuffer.clear();
    }
    
    reflectionInBuffer.copyFrom(0, 0, buffer.getReadPointer(0), numberOfSamples);
    reverbInBuffer.copyFrom(0, 0, buffer.getReadPointer(0), numberOfSamples);

    
    float* outBufferL = buffer.getWritePointer (0);
    float* outBufferR = buffer.getWritePointer (1);
    const float* inBuffer = buffer.getReadPointer(0);
    
    const float* inBufferRefl = reflectionInBuffer.getReadPointer(0);
    const float* inBufferReverb = reverbInBuffer.getReadPointer(0);
    float* outBufferReverbL = reverbOutBuffer.getWritePointer(0);
    float* outBufferReverbR = reverbOutBuffer.getWritePointer(1);

    
    directSource.process(inBuffer, outBufferL, outBufferR, numberOfSamples, params);

    //buffer.clear();
    float gain = 1.0;
    if(params.distanceSimulationParam->get() || metadata_sofafile.hasMultipleDistances){
        if(params.distanceParam->get() > 0.1)
             gain = 1 / params.distanceParam->get();
        buffer.applyGain(gain);
        
        
        //Nearfield Simulation: Increasing IID effect
        float distance = params.distanceParam->get();
        if(params.nearfieldSimulationParam->get() && distance < 1.0 && distance > 0.2){
        
            //alpha_az determines how much the increasing IID effect will be applied, because it is dependent on the azimuth angle
            float alpha_az = powf( sinf(  params.azimuthParam->get() * 2.0 * M_PI  ), 3 ); //Running from 0 -> 1 -> 0 -> -1 -> 0 for a full circle. The ^3 results in a steeper sine-function, so that the effect will be less present in the areas around 0° or 180°, but emphasized for angles 90° or 270°, where the source is cleary more present to one ear, while the head masks the other ear
            
            //alpha_el weakens the effect, if the source is elevated above or below the head, because shadowing of the head will be less present. It is 1 for zero elevation and moves towards zero for +90° and -90°
            float alpha_el = cosf(params.elevationParam->get() * 2.0 * M_PI);
        
            float normGain = 7.8 * (1.0 - distance) * (1.0 - distance); //will run exponentially from +0db to ~+5db when distance goes from 1m to 0.2m
        
            float IID_gain_l = Decibels::decibelsToGain(   (normGain * -alpha_az * alpha_el)  ); // 0db -> 0...-5db -> 0db -> 0..5db
            float IID_gain_r = Decibels::decibelsToGain(  normGain * alpha_az * alpha_el); // 0db -> 0...5db -> 0db -> 0..-5db
        
            buffer.applyGain(0, 0, numberOfSamples, IID_gain_l);
            buffer.applyGain(1, 0, numberOfSamples, IID_gain_r);
        }


    }
    

    
    if(params.distanceSimulationParam->get()){
        
        if(params.mirrorSourceParam->get()){
            int order = 1;
            float azimuthRefl[numReflections];
            float distanceRefl[numReflections];
            
            float xPos, yPos;
            const float roomRadius = 5.0;
            float xSource = sinf(params.azimuthParam->get() * M_PI / 180.0) * cosf(params.elevationParam->get() * M_PI / 180.0) * params.distanceParam->get();
            float ySource = cosf(params.azimuthParam->get() * M_PI / 180.0) * cosf(params.elevationParam->get() * M_PI / 180.0) * params.distanceParam->get();
            
            Point<float> origin = Point<float>(0.0, 0.0);
            float azimuth = 2.0* M_PI * params.azimuthParam->get();
            float distance = params.distanceParam->get();
            Point<float> sourcePos = origin.getPointOnCircumference(distance, azimuth);
            sourcePos.y *= -1.0;
            printf("\n SOURCE Azimuth: %.2f, Distance: %.2f", azimuth, distance);
            printf("\n SOURCE X: %.2f, Y: %.2f", sourcePos.getX(), sourcePos.getY());
                                                
            for(int i = 0; i < numReflections; i++){
                reflections[i].process(inBufferRefl, outBufferL, outBufferR, numberOfSamples, sourcePos);
            }
        }else{
        
            float alpha_s = sinf(params.azimuthParam->get() * 2.0 * M_PI); //Running from 0 -> 1 -> 0 -> -1 -> 0 for a full circle
            float alpha_c = cosf(params.azimuthParam->get() * 2.0 * M_PI); //Running from 1 -> 0 -> -1 -> 0 -> 1 for a full circle
            float delay[4], damp[4];
            
            float distance = 1.0;
            if(params.distanceSimulationParam || metadata_sofafile.hasMultipleDistances){
                distance = params.distanceParam->get();
            }
            //clip distance, to avoid negative delay values. More a dirty solution, because in the edges of a 10x10m room, where the distance is larger than 5m, there is no change in the reflections anymore. This might be neglible, because the reflections are only an approximation anyway
            if(distance > 5.0){
                distance = 5.0;
            }
            const float roomRadius = 5; //10x10m room
            const float speedOfSound = 343.2;
            const float meterToMs = 1000.0 / speedOfSound;
            delay[0] = meterToMs * ((2.0 * roomRadius - distance) - alpha_s * distance);
            delay[1] = meterToMs * ((2.0 * roomRadius - distance) + alpha_s * distance);
            delay[1] = meterToMs * ((2.0 * roomRadius - distance) - alpha_c * distance);
            delay[1] = meterToMs * ((2.0 * roomRadius - distance) + alpha_c * distance);

            damp[0] = 1.0/(2*roomRadius-alpha_s * distance);
            damp[1] = 1.0/(2*roomRadius+alpha_s * distance);
            damp[2] = 1.0/(2*roomRadius-alpha_c * distance);
            damp[3] = 1.0/(2*roomRadius+alpha_c * distance);
            
            for(int i=0; i < 2; i++){
                earlyReflections[i]->process(inBufferRefl, outBufferL, outBufferR, numberOfSamples, delay[i], damp[i]);
            }
        }
        
        
        reverb.processBlockMS(inBufferReverb, outBufferReverbL, outBufferReverbR, numberOfSamples, params.testSwitchParam->get());
        reverbOutBuffer.applyGain(0.05);
        
        buffer.addFrom(0, 0, reverbOutBuffer, 0, 0, numberOfSamples);
        buffer.addFrom(1, 0, reverbOutBuffer, 1, 0, numberOfSamples);
        
        
    }
    
    buffer.applyGain(0.25);


}

//==============================================================================
bool SofaPanAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* SofaPanAudioProcessor::createEditor()
{
    return new SofaPanAudioProcessorEditor (*this);
}

//==============================================================================
void SofaPanAudioProcessor::getStateInformation (MemoryBlock& destData)
{
//    ScopedPointer<XmlElement> xml (new XmlElement ("SofaPanSave"));
//    xml->setAttribute ("gain", (double) *gain);
//    copyXmlToBinary (*xml, destData);
}

void SofaPanAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SofaPanAudioProcessor();
}



void SofaPanAudioProcessor::setSOFAFilePath(String sofaString)
{
    pathToSOFAFile = sofaString;
    initData(pathToSOFAFile);
    
    if(usingGlobalSofaFile){
        MemoryBlock message;
        const char* messageText = pathToSOFAFile.toRawUTF8();
        size_t messageSize = pathToSOFAFile.getNumBytesAsUTF8();
        message.append(messageText, messageSize);
        
        sendMessage(message);
    }
    
    
}

fftwf_complex* SofaPanAudioProcessor::getCurrentHRTF()
{
    if(HRTFs == NULL)
        return NULL;
    
    float azimuth = params.azimuthParam->get() * 360.0;
    float elevation = (params.elevationParam->get()-0.5) * 180.0;
    float distance = 1;
    if(!(bool)params.distanceSimulationParam->get())
        distance = params.distanceParam->get();
    
    return HRTFs->getHRTFforAngle(elevation, azimuth, distance);
}

float* SofaPanAudioProcessor::getCurrentHRIR()
{

    if(HRTFs == NULL)
        return NULL;
    
    float azimuth = params.azimuthParam->get() * 360.0;
    float elevation = (params.elevationParam->get()-0.5) * 180.0;
    
    float distance = 1;
    if(!(bool)params.distanceSimulationParam->get())
        distance = params.distanceParam->get();
    
    return HRTFs->getHRIRForAngle(elevation, azimuth, distance);
}

float SofaPanAudioProcessor::getCurrentITD()
{
    
    if(HRTFs == NULL)
        return NULL;
    
    float azimuth = params.azimuthParam->get() * 360.0;
    float elevation = (params.elevationParam->get()-0.5) * 180.0;
    
    float distance = 1;
    if(!(bool)params.distanceSimulationParam->get())
        distance = params.distanceParam->get();
    
    return HRTFs->getITDForAngle(elevation, azimuth, distance);
}

int SofaPanAudioProcessor::getSampleRate()
{
    return (int)sampleRate_f;
}

int SofaPanAudioProcessor::getComplexLength()
{
    return directSource.getComplexLength();
}

void SofaPanAudioProcessor::messageReceived (const MemoryBlock &message){
    
    if(usingGlobalSofaFile){
        String newFilePath = message.toString();
        printf("\n%s: Set New File Path: %s", getPipe()->getName().toRawUTF8(), newFilePath.toRawUTF8());
    
        initData(newFilePath);
    }
    
}

void SofaPanAudioProcessor::setUsingGlobalSofaFile(bool useGlobal){
    if(useGlobal){
        String path = updater->requestCurrentFilePath();
        if(path.length() > 1 && path!=pathToSOFAFile){
            pathToSOFAFile = path;
            initData(pathToSOFAFile);
        }
        usingGlobalSofaFile = true;
    }else{
        usingGlobalSofaFile = false;
    }
}

bool SofaPanAudioProcessor::getUsingGlobalSofaFile(){
    return usingGlobalSofaFile;
}

