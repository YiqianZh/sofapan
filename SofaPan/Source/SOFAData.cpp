/*
  ==============================================================================

    SOFAData.cpp
    Created: 4 Apr 2017 11:47:12am
    Author:  David Bau

  ==============================================================================
*/

#include "SOFAData.h"


int getIRLengthForNewSampleRate(int IR_Length, int original_SR, int new_SR);
//void error(char* errorMessage);
int sampleRateConversion(float* inBuffer, float* outBuffer, int n_inBuffer, int n_outBuffer, int originalSampleRate, int newSampleRate);

SOFAData::SOFAData(){
    
    loadedHRIRs = NULL;
    currentFilePath = NULL;
    currentSampleRate = 0;

    
}

SOFAData::~SOFAData(){
    
    if(loadedHRIRs != NULL){
        for(int i = 0; i < sofaMetadata.numMeasurements; i++){
            delete loadedHRIRs[i];
        }
        free(loadedHRIRs);
    }
}

void SOFAData::initSofaData(const char* filePath, int sampleRate)
{

    printf("\n Attempting to load SofaFile...");
    
    //This line makes sure that the init is not always performed when a new instance of the plugin is created
    if(filePath == currentFilePath && sampleRate == currentSampleRate)
        return;
    
    printf("\n Loading File");

    
    if(loadedHRIRs != NULL){
        for(int i = 0; i < sofaMetadata.numMeasurements; i++){
            delete loadedHRIRs[i];
        }
        free(loadedHRIRs);
    }
    
    // LOAD SOFA FILE
    int status = loadSofaFile(filePath, sampleRate);
    if(status){
        errorHandling(status);
        createPassThrough_FIR(sampleRate);
    }
    
    
    //Normalisation, because some institutions provide integer or too high sample values
    float maxValue = 0.0;
    for(int i = 0; i < sofaMetadata.numMeasurements; i++){
        for(int j = 0; j < lengthOfHRIR * 2; j++){
            if (fabs(loadedHRIRs[i]->getHRIR()[j]) > maxValue)
                maxValue = fabs(loadedHRIRs[i]->getHRIR()[j]);
        }
    }
    
    float normalisation = 1.0;
    if(maxValue > 100){
        normalisation = 1.0/maxValue;

    }
    
    for(int i = 0; i < sofaMetadata.numMeasurements; i++){
        for(int j = 0; j < lengthOfHRIR * 2; j++){
            loadedHRIRs[i]->getHRIR()[j] *= normalisation;
        }
    }
    
    //Allocate and init FFTW
    float* fftInputBuffer = fftwf_alloc_real(lengthOfFFT);
    fftwf_complex* fftOutputBuffer = fftwf_alloc_complex(lengthOfHRTF);
    fftwf_plan FFT = fftwf_plan_dft_r2c_1d(lengthOfFFT, fftInputBuffer, fftOutputBuffer, FFTW_ESTIMATE);
    
    for(int i = 0; i < sofaMetadata.numMeasurements; i++){
        // LEFT
        
        //Write IR into inputBuffer and do zeropadding
        for(int k = 0; k < lengthOfHRIR; k++){
            fftInputBuffer[k] = loadedHRIRs[i]->getHRIR()[k];
            fftInputBuffer[k+lengthOfHRIR] = 0.0;
        }
        
        //Do FFT HRIR->HRTF
        fftwf_execute(FFT);
        
        //Save TF
        for(int j = 0; j < lengthOfHRTF; j++){
            loadedHRIRs[i]->getHRTF()[j][0] = fftOutputBuffer[j][0]; //RE
            loadedHRIRs[i]->getHRTF()[j][1] = fftOutputBuffer[j][1]; //IM
        }
        
        // RIGHT
        
        for(int k = 0; k < lengthOfHRIR; k++){
            fftInputBuffer[k] = loadedHRIRs[i]->getHRIR()[k + lengthOfHRIR];
            fftInputBuffer[k+lengthOfHRIR] = 0.0;
        }
        
        fftwf_execute(FFT);
        
        for(int j = 0; j < lengthOfHRTF; j++){
            loadedHRIRs[i]->getHRTF()[j+lengthOfHRTF][0] = fftOutputBuffer[j][0]; //RE
            loadedHRIRs[i]->getHRTF()[j+lengthOfHRTF][1] = fftOutputBuffer[j][1]; //IM
        }
        
    }
    
    fftwf_free(fftInputBuffer);
    fftwf_free(fftOutputBuffer);
    fftwf_destroy_plan(FFT);
    
}
int SOFAData::getLengthOfHRIR(){
    return lengthOfHRIR;
}


fftwf_complex* SOFAData::getHRTFforAngle(float elevation, float azimuth, float radius){
 
    int best_id = 0;
    
    float delta;
    float min_delta = 1000;
    for(int i = 0; i < sofaMetadata.numMeasurements; i++){
        delta = fabs(loadedHRIRs[i]->Elevation - elevation)
        + fabs(loadedHRIRs[i]->Azimuth - azimuth)
        + fabs(loadedHRIRs[i]->Distance - radius);
        if(delta < min_delta){
            min_delta = delta ;
            best_id = i;
        }
    }
    return loadedHRIRs[best_id]->getHRTF();
}

float* SOFAData::getHRIRForAngle(float elevation, float azimuth, float radius){
    int best_id = 0;
    
    float delta;
    float min_delta = 1000;
    for(int i = 0; i < sofaMetadata.numMeasurements; i++){
        delta = fabs(loadedHRIRs[i]->Elevation - elevation)
        + fabs(loadedHRIRs[i]->Azimuth - azimuth)
        + fabs(loadedHRIRs[i]->Distance - radius);
        if(delta < min_delta){
            min_delta = delta ;
            best_id = i;
        }
    }
    return loadedHRIRs[best_id]->getHRIR();
    
    
}

int SOFAData::loadSofaFile(const char* filePath, int hostSampleRate){
    
    /* Some things are defined as variables in the SOFA-Convention, but are assumed here for the sake of simplicity:
     - Data.SamplingRate.Units is always "hertz"
     - Data.Delay is Zero, the TOA information is contained in the FIRs
     - Data.IR has always three dimensions
     - Data.IR has two receivers = two ears
     
     Furthermore, by now the only accepted DataType is "FIR" and SOFAConvention is "SimpleFreeFieldHRIR"
    */
    
    //open File in read mode (NC_NOWRITE) and get netcdf-ID of Sofa-File
    int  status;               /* error status */
    int  ncid;                 /* netCDF ID */
    if ((status = nc_open(filePath, NC_NOWRITE, &ncid)))
        return ERR_OPENFILE;
    
    
    
    int numberOfAttributes;
    nc_inq(ncid, NULL, NULL, &numberOfAttributes, NULL);
    
    char name_of_att[NC_MAX_NAME + 1][numberOfAttributes];
    char* attributes[numberOfAttributes];
    
    sofaMetadata.globalAttributeNames.resize(0);
    sofaMetadata.globalAttributeValues.resize(0);
    
    for(int i = 0; i < numberOfAttributes; i++){
        nc_inq_attname(ncid, NC_GLOBAL, i, name_of_att[i]);
        
        size_t attlength;
        nc_inq_attlen(ncid, NC_GLOBAL, name_of_att[i], &attlength);
        
        char att[attlength + 1];
        nc_get_att(ncid, NC_GLOBAL, name_of_att[i], &att);
        att[attlength] = '\0';
        attributes[i] = att;
        
        sofaMetadata.globalAttributeNames.add(String(CharPointer_UTF8 (name_of_att[i])));
        sofaMetadata.globalAttributeValues.add(String(CharPointer_UTF8 (attributes[i])));
        
    }
    
    
    
    
    /* -- check if attribute "SOFAConventions" is "SimpleFreeFieldHRIR": -- */
    String sofa_conv = getSOFAGlobalAttribute("SOFAConventions", ncid);
    if(sofa_conv.compare("SimpleFreeFieldHRIR")){
        nc_close(ncid);
        return ERR_NOTSUP;
    }
    sofaMetadata.SOFAConventions = sofa_conv;
    
    String data_type = getSOFAGlobalAttribute("DataType", ncid);
    if(data_type.compare("FIR")){
        nc_close(ncid);
        return ERR_NOTSUP;
    }
    sofaMetadata.dataType = data_type;

    //Get Sampling Rate
    int SamplingRate, SamplingRate_id;
    status = nc_inq_varid(ncid, "Data.SamplingRate", &SamplingRate_id);
    status += nc_get_var_int(ncid, SamplingRate_id, &SamplingRate);
    if(status != NC_NOERR){
        //error((char*)"Load Sofa: Could not read Sample Rate");
        nc_close(ncid);
        return ERR_READFILE;
    }
    sofaMetadata.sampleRate = SamplingRate;
    
    
    //Get various Metadata
    sofaMetadata.listenerShortName = getSOFAGlobalAttribute("ListenerShortName", ncid);
    /*
     .
     .  more to be added
     .
     */

    
    
    //Get netcdf-ID of IR Data
    int DataIR_id;
    if ((status = nc_inq_varid(ncid, "Data.IR", &DataIR_id)))//Get Impulse Resopnse Data ID
        return ERR_READFILE;
    
    //Get Dimensions of Data IR
    int DataIR_dimidsp[MAX_VAR_DIMS];
    size_t dimM_len;//Number of Measurements
    size_t dimR_len;//Number of Receivers, in case of two ears dimR_len=2
    size_t dimN_len;//Number of DataSamples describing each Measurement
    
    if ((status = nc_inq_var(ncid, DataIR_id, 0, 0, 0, DataIR_dimidsp, 0)))
        return ERR_READFILE;
    if ((status = nc_inq_dimlen(ncid, DataIR_dimidsp[0], &dimM_len)))
        return ERR_READFILE;
    if ((status = nc_inq_dimlen(ncid, DataIR_dimidsp[1], &dimR_len)))
        return ERR_READFILE;
    if ((status = nc_inq_dimlen(ncid, DataIR_dimidsp[2], &dimN_len)))
        return ERR_READFILE;

    sofaMetadata.numSamples = dimN_len; //Store Value in Struct metadata_struct
    sofaMetadata.numMeasurements = dimM_len;
    sofaMetadata.numReceivers = dimR_len;
    
    // Übergangslösung: Es werden dateien mit mehereren Receiverkanälen akzepiert, aber statisch die ersten beiden kanäle verwendet
    int receiver_for_R = 0;
    int receiver_for_L = 1;
    
    //now that the sampleRate of the SOFA, the host sample rate and the length N is known, the length of the interpolated HRTF can be calculated
    lengthOfHRIR = getIRLengthForNewSampleRate(dimN_len, sofaMetadata.sampleRate, hostSampleRate);
    lengthOfFFT = 2 * lengthOfHRIR;
    lengthOfHRTF = (lengthOfFFT * 0.5) + 1;
    
    
    //Get Source Positions (Azimuth, Elevation, Distance)
    int SourcePosition_id;
    if ((status = nc_inq_varid(ncid, "SourcePosition", &SourcePosition_id)))
        return ERR_READFILE;
    float* SourcePosition = NULL;
    SourcePosition = (float*)malloc(sizeof(float) * 3 * dimM_len); //Allocate Memory for Sourcepositions of each Measurement
    if(SourcePosition == NULL)
        return ERR_MEM_ALLOC;
    if ((status = nc_get_var_float(ncid, SourcePosition_id, SourcePosition)))// Store Sourceposition Data to Array
    {
        free(SourcePosition);
        return ERR_READFILE;
    };
    
    //Get Impluse Responses
    float *DataIR = NULL;
    DataIR = (float*)malloc(dimM_len * dimR_len * dimN_len * sizeof(float));
    if(DataIR == NULL)
        return ERR_MEM_ALLOC;
    if ((status = nc_get_var_float(ncid, DataIR_id, DataIR))) //Read and write Data IR to variable Data IR
    {
        free(SourcePosition);
        free(DataIR);
        return ERR_READFILE;
    };
    
    int numZeroElevation = 0;
    int u = 0;
    for (int i = 0; i < dimM_len; i++) {
        if (SourcePosition[u + 1] == 0) {
            numZeroElevation++;
        }
        u += 3;
    }
    sofaMetadata.numMeasurements0ev = numZeroElevation;
    
    loadedHRIRs = (Single_HRIR_Measurement**)malloc(sofaMetadata.numMeasurements * sizeof(Single_HRIR_Measurement));
    if(loadedHRIRs == NULL){
        free(SourcePosition);
        free(DataIR);
        return ERR_MEM_ALLOC;
    }

    
    sofaMetadata.minElevation = 0.0;
    sofaMetadata.maxElevation = 0.0;
    sofaMetadata.minDistance = 1000.0;
    sofaMetadata.maxDistance = 0.0;
    
    int i = 0, j = 0, l = 0, x = 0;
    float *IR_Left = (float *)malloc(dimN_len * sizeof(float));
    float *IR_Right = (float *)malloc(dimN_len * sizeof(float));
    float *IR_Left_Interpolated = (float *)malloc(lengthOfHRIR * sizeof(float));
    float *IR_Right_Interpolated = (float *)malloc(lengthOfHRIR * sizeof(float));
    
    for (i = 0; i < dimM_len; i++) {
        
        if(SourcePosition[l+1] < sofaMetadata.minElevation) sofaMetadata.minElevation = SourcePosition[l+1];
        if(SourcePosition[l+1] > sofaMetadata.maxElevation) sofaMetadata.maxElevation = SourcePosition[l+1];
        if(SourcePosition[l+2] < sofaMetadata.minDistance) sofaMetadata.minDistance = SourcePosition[l+2];
        if(SourcePosition[l+2] > sofaMetadata.maxDistance) sofaMetadata.maxDistance = SourcePosition[l+2];
        
        Single_HRIR_Measurement *measurement_object = new Single_HRIR_Measurement(lengthOfHRIR, lengthOfHRTF);
        //Temporary storage of HRIR-Data
        
        
        for (j = 0; j < dimN_len; j++) {
            IR_Right[j] = DataIR[i*(dimR_len*dimN_len) + receiver_for_R * dimN_len + j];
            
            IR_Left[j] = DataIR[i*(dimR_len*dimN_len) + receiver_for_L * dimN_len + j];
            
        };
        
        sampleRateConversion(IR_Right, IR_Right_Interpolated, dimN_len, lengthOfHRIR, sofaMetadata.sampleRate, hostSampleRate);
        sampleRateConversion(IR_Left, IR_Left_Interpolated, dimN_len, lengthOfHRIR, sofaMetadata.sampleRate, hostSampleRate);
        
        for(int i = 0; i < lengthOfHRIR; i++){
            measurement_object->getHRIR()[i] = IR_Left_Interpolated[i];
            measurement_object->getHRIR()[i+lengthOfHRIR] = IR_Right_Interpolated[i];
        }
        
        
        
        float wrappedSourceposition = fmodf((SourcePosition[l] + 360.0), 360.0);

        measurement_object->setValues(wrappedSourceposition, SourcePosition[l + 1], SourcePosition[l + 2]);
        measurement_object->index = i;
        loadedHRIRs[i] = measurement_object;
        
        x++;
        
        l += 3;
    };
    
    sofaMetadata.hasMultipleDistances = sofaMetadata.maxDistance != sofaMetadata.minDistance ? true : false;
    
    free(IR_Left);
    free(IR_Right);
    free(IR_Left_Interpolated);
    free(IR_Right_Interpolated);
    
    
    
    free(SourcePosition);
    free(DataIR);
    nc_close(ncid);

    //SOFAFile_loaded_flag = 1;
    return 0;

}



sofaMetadataStruct SOFAData::getMetadata(){
    return sofaMetadata;
}



#pragma mark HELPERS

int sampleRateConversion(float* inBuffer, float* outBuffer, int n_inBuffer, int n_outBuffer, int originalSampleRate, int newSampleRate){
    
    
    float frequenzFaktor = (float)originalSampleRate / (float)newSampleRate;
    float f_index = 0.0f;
    int i_index = 0;
    float f_bruch = 0;
    int i_fganzezahl = 0;
    while ((i_fganzezahl+1) < n_inBuffer){
        if(i_index >= n_outBuffer || (i_fganzezahl+1) >= n_inBuffer) return 1;
        //Linear interpolieren
        outBuffer[i_index] = inBuffer[i_fganzezahl] * (1.0f - f_bruch) + inBuffer[i_fganzezahl + 1] * f_bruch;
        outBuffer[i_index] *= frequenzFaktor;
        //Berechnungen fuer naechste Runde.
        i_index++;
        f_index = i_index * frequenzFaktor;
        i_fganzezahl = (int)f_index;
        f_bruch = f_index - i_fganzezahl;
    }
    while(i_index < n_outBuffer){
        outBuffer[i_index] = 0.0;
        i_index++;
    }
    
    return 0;
    
}


int getIRLengthForNewSampleRate(int IR_Length, int original_SR, int new_SR){
    
    float frequenzFaktor = (float)original_SR / (float)new_SR;
    float newFirLength_f = (float)(IR_Length) / frequenzFaktor; //z.B. 128 bei 44.1kHz ---> 128/(44.1/96) = 278.6 bei 96kHz
    int fir_length = 2;
    while (fir_length < newFirLength_f) {
        fir_length *= 2;
    }
    
    return fir_length;
}


void SOFAData::errorHandling(int status) {
    String ErrorMessage;
    switch (status) {
        case ERR_MEM_ALLOC:
            ErrorMessage = "Error: Memory allocation";
            break;
        case ERR_READFILE:
            ErrorMessage = "Error while reading from File";
            break;
        case ERR_NOTSUP:
            ErrorMessage = "This file contains data that is not supported";
            break;
        case ERR_OPENFILE:
            ErrorMessage = "Could not open file";
            break;
        case ERR_UNKNOWN:
            ErrorMessage = "An Error occured during loading the File";
            break;
        default:
            ErrorMessage = "An Error occured during loading the File";
            break;
    }
    
    
    AlertWindow::showNativeDialogBox("Binaural Renderer", ErrorMessage, false);
    

}
//
//
//
void SOFAData::createPassThrough_FIR(int _sampleRate){

    const int passThroughLength = 512;
    sofaMetadata.sampleRate = _sampleRate;
    sofaMetadata.numMeasurements = 1;
    sofaMetadata.numSamples = passThroughLength;
    sofaMetadata.dataType = String ("FIR");
    sofaMetadata.SOFAConventions = String ("Nope");
    sofaMetadata.listenerShortName = String ("Nope");

    sofaMetadata.minElevation =0.0;
    sofaMetadata.maxElevation =0.0;

    lengthOfHRIR = passThroughLength;
    lengthOfFFT = 2 * lengthOfHRIR;
    lengthOfHRTF = (lengthOfFFT * 0.5) + 1;
 
    loadedHRIRs = (Single_HRIR_Measurement**)malloc(1 * sizeof(Single_HRIR_Measurement));
    Single_HRIR_Measurement *measurement_object = new Single_HRIR_Measurement(lengthOfHRIR, lengthOfHRTF);

    float *IR_Left = (float *)malloc(passThroughLength * sizeof(float));
    float *IR_Right = (float *)malloc(passThroughLength * sizeof(float));
    
    IR_Left[0]  = 1.0;
    IR_Right[0] = 1.0;
    for (int j = 1; j < passThroughLength; j++) {
        IR_Left[j]  = 0.0;
        IR_Right[j] = 0.0;
    }
    
    
    for(int i = 0; i < lengthOfHRIR; i++){
        measurement_object->getHRIR()[i] = IR_Left[i];
        measurement_object->getHRIR()[i+lengthOfHRIR] = IR_Right[i];
    }

    measurement_object->setValues(0.0, 0.0 , 0.0);
    measurement_object->index = 0;
    loadedHRIRs[0] = measurement_object;
    free(IR_Left);
    free(IR_Right);
    
}

String SOFAData::getSOFAGlobalAttribute(const char* attribute_ID, int ncid){
    size_t att_length = 0;
    
    //get length if possible


    if(nc_inq_attlen(ncid, NC_GLOBAL, attribute_ID, &att_length))
        return String("- Unknown - ");

    
    //get value if possible
    char att[att_length + 1];
    if(nc_get_att(ncid, NC_GLOBAL, attribute_ID, &att))
        return String("- Unknown - ");
    
    //terminate string manually
    att[att_length] = '\0';
    
    return String(att);
}
