/**
  Generated Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This is the main file generated using PIC10 / PIC12 / PIC16 / PIC18 MCUs

  Description:
    This header file provides implementations for driver APIs for all modules selected in the GUI.
    Generation Information :
        Product Revision  :  PIC10 / PIC12 / PIC16 / PIC18 MCUs - 1.81.7
        Device            :  PIC16LF1847
        Driver Version    :  2.00
*/

/*
    (c) 2018 Microchip Technology Inc. and its subsidiaries. 
    
    Subject to your compliance with these terms, you may use Microchip software and any 
    derivatives exclusively with Microchip products. It is your responsibility to comply with third party 
    license terms applicable to your use of third party software (including open source software) that 
    may accompany Microchip software.
    
    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER 
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY 
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS 
    FOR A PARTICULAR PURPOSE.
    
    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND 
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP 
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO 
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL 
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT 
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS 
    SOFTWARE.
*/




#include "mcc_generated_files/mcc.h"
#include "main.h"
#include "i2c.h"
#include "isl94208.h"
#include "config.h"
#include "thermistor.h"
#include "led_blink_pattern.h"

void Set_LED_RGB(uint8_t RGB){  //Accepts binary input 0b000. Bit 2 = Red Enable. Bit 1 = Green Enable. Bit 0 = Red Enable. R.G.B.
    if (RGB & 0b001){
        blueLED = 0;
    }
    else{
        blueLED = 1;
    }
    
    if (RGB & 0b010){
        greenLED = 0;
    }
    else{
        greenLED = 1;
    }
    
    if (RGB & 0b100){
        redLED = 0;
    }
    else{
        redLED = 1;
    }
}

void ClearI2CBus(){
    uint8_t initialState[] = {TRIS_SDA, TRIS_SCL, ANS_SDA, ANS_SCL, SDA, SCL, SSP1CON1bits.SSPEN}; //Backup initial pin setup state
    SSP1CON1bits.SSPEN = 0;     //Disable MSSP if enabled
    TRIS_SDA = 1;     //SDA - Set Data as input
    TRIS_SCL = 1;     //SCL - Set Clock as input = high-impedance output
    ANS_SDA = 0;      //Set both pins as digital
    ANS_SCL = 0;
    SCL = 0;    //Set SCL PORT to be low, then we'll toggle TRIS to switch between low and high-impedance
    
    uint8_t validOnes = 0;
    
    //Clock the SCL pin until we get 10 valid ones in a row like we'd expect from an idle bus
    while (validOnes < 10){
        TRIS_SCL = 0; //Clock low
        __delay_us(5); //Wait out clock low period
        TRIS_SCL = 1; //Clock high
        __delay_us(2.5);  //Wait until we are in the mid point of clock high period
        if (SDA == 1){  //Read data and check if SDA is high (idle)
            validOnes++;
        }
        else{
            validOnes = 0;  //if the data isn't a one, reset the counter so we get 10 in a row
        }
        __delay_us(2.5); //Wait remainder of clock high period
    }
    TRIS_SDA = (__bit) initialState[0];   //Restore initial pin I/O state
    TRIS_SCL = (__bit) initialState[1];
    ANS_SDA = (__bit) initialState[2];
    ANS_SCL = (__bit) initialState[3];
    SDA = (__bit) initialState[4];
    SCL = (__bit) initialState[5];
    SSP1CON1bits.SSPEN = (__bit) initialState[6];
    if (initialState[6]){
        ISL_Init();
    }
    I2C_ERROR_FLAGS = 0;
}

uint16_t static ConvertADCtoMV(uint16_t adcval){                //I included this function here and in isl94208 so that it could stand alone if needed. There is probably a better way to do this.
    return (uint16_t) ((uint32_t)adcval * VREF_VOLTAGE_mV / 1024);
}

void ADCPrepare(void){
    DAC_SetOutput(0);   //Make sure DAC is set to 0V
    ADC_SelectChannel(ADC_PIC_DAC); //Connect ADC to 0V to empty internal ADC sample/hold capacitor
    __delay_us(1);  //Wait a little bit
}

uint16_t readADCmV(adc_channel_t channel){        //Adds routine to switch to DAC VSS output to clear sample/hold capacitor before taking real reading
    ADCPrepare();
    return ConvertADCtoMV( ADC_GetConversion(channel) ); //Finally run the conversion and store the result
}

uint16_t dischargeIsense_mA(void){
    ADCPrepare();
    uint16_t adcval = ADC_GetConversion(ADC_DISCHARGE_ISENSE);
    return (uint16_t) ((uint32_t)adcval * VREF_VOLTAGE_mV * 1000 / 1024 / 2);  //This better maintains precision by doing the multiplication for 2500mV VREF and 1000mA/A in one step as a uint32_t. Then we divide by 1024 ADC steps and the 2mOhm shunt resistor.
}

detect_t checkDetect(void){
    uint16_t result = readADCmV(ADC_CHRG_TRIG_DETECT);
    if (result > DETECT_CHARGER_THRESH_mV){
        return CHARGER;
    }
    else if (result < DETECT_CHARGER_THRESH_mV && result > DETECT_TRIGGER_THRESH_mV){
        return TRIGGER;
    }
    else{
        return NONE;
    }
}

modelnum_t checkModelNum (void){
/* This function assumes that if the reading of the thermistor from the ISL is
 * > 100mV above the reading from the PIC, we must be using an SV09 board 
 * which has an opamp driving the ISL thermistor input to ~3.3V until the thermistor voltage
 * goes below ~820mV, then it drives the ISL input to ~0V.
 * The SV11 has the thermistor input to the ISL and PIC tied together since
 * there is just the thermistor with one pull-up resistor. It uses a different voltage scale though. */
    uint16_t isl_thermistor_reading = ISL_GetAnalogOutmV(AO_EXTTEMP);
    uint16_t pic_thermistor_reading = readADCmV(ADC_THERMISTOR);
    int16_t delta = (int16_t)isl_thermistor_reading - (int16_t)pic_thermistor_reading;
    if (delta > 100){
        return SV09;
    }
    else{
        return SV11;
    }
}

bool safetyChecks (void){
    bool result = true;
    result &= (isl_int_temp < MAX_DISCHARGE_TEMP_C);        //Internal ISL temp is OK
    result &= (thermistor_temp < MAX_DISCHARGE_TEMP_C);    //Thermistor temp is OK
    result &= (ISL_RegData[Status] == 0);               //No ISL error flags
    result &= (discharge_current_mA < MAX_DISCHARGE_CURRENT_mA);     //We aren't discharging more than 30A
    
    if (!result && state != ERROR){         //Makes sure we don't write new errors in to past_error_reason while we are in the error state
        setErrorReasonFlags(&past_error_reason);
    }
    
    return result;
}

bool minCellOK(void){
    return (cellstats.mincell_mV > MIN_DISCHARGE_CELL_VOLTAGE_mV);
}

bool maxCellOK(void){
    return (cellstats.maxcell_mV < MAX_CHARGE_CELL_VOLTAGE_mV);
}

bool chargeTempCheck(void){
    bool result = true;
    result &= (isl_int_temp < MAX_CHARGE_TEMP_C);
    result &= (thermistor_temp < MAX_CHARGE_TEMP_C);
    
    if (!result && state != ERROR){
        setErrorReasonFlags(&past_error_reason);
    }
    return result;
}


/* Most of the time the result of this function call will be stored in past_error_reason so that once we are in the actual error state, we still have a record of why we got there.
 * Once we are in the error state, we will repeatedly clear current_error_reason and store the result of this function in it so we can see the actual reason we haven't left the error state yet.
 * We were previously assuming that if we entered the error state and saw that the charger was connected and we were over the charge temp limit, that must be the reason for entry.
 * Thus, we had a flag that wouldn't let us leave the error state until we'd actually met the lower charge temp limit requirements.
 * Now that we are actually recording the reason for an error when it occurs, before any resolution has been taken,
 *  we can check that actual data to determine if we should be using the stricter charging temp limits.
 */
void setErrorReasonFlags(volatile error_reason_t *datastore){
    datastore->ISL_INT_OVERTEMP_FLAG |= ISL_GetSpecificBits_cached(ISL.INT_OVER_TEMP_STATUS);
    datastore->ISL_EXT_OVERTEMP_FLAG |= ISL_GetSpecificBits_cached(ISL.EXT_OVER_TEMP_STATUS);
    datastore->ISL_INT_OVERTEMP_PICREAD |= !(isl_int_temp < MAX_DISCHARGE_TEMP_C);
    datastore->THERMISTOR_OVERTEMP_PICREAD |= !(thermistor_temp < MAX_DISCHARGE_TEMP_C);
    datastore->CHARGE_OC_FLAG |= ISL_GetSpecificBits_cached(ISL.OC_CHARGE_STATUS);
    datastore->DISCHARGE_OC_FLAG |= ISL_GetSpecificBits_cached(ISL.OC_DISCHARGE_STATUS);
    datastore->DISCHARGE_SC_FLAG |= ISL_GetSpecificBits_cached(ISL.SHORT_CIRCUIT_STATUS);
    datastore->DISCHARGE_OC_SHUNT_PICREAD |= !(discharge_current_mA < MAX_DISCHARGE_CURRENT_mA);
    datastore->CHARGE_ISL_OVERTEMP_PICREAD |= (state == CHARGING && !(isl_int_temp < MAX_CHARGE_TEMP_C));
    datastore->CHARGE_THERMISTOR_OVERTEMP_PICREAD |= (state == CHARGING && !(thermistor_temp < MAX_CHARGE_TEMP_C));
    datastore->DETECT_MODE = detect;
    

    if (state == ERROR) { //If we are in the error state, we need to check if we are in hysteresis violation 
        datastore->TEMP_HYSTERESIS |= (isl_int_temp < MAX_DISCHARGE_TEMP_C && !(isl_int_temp + HYSTERESIS_TEMP_C < MAX_DISCHARGE_TEMP_C));  //Hysteresis only matters if we aren't over the main temp. limit.
        datastore->TEMP_HYSTERESIS |= (thermistor_temp < MAX_DISCHARGE_TEMP_C && !(thermistor_temp + HYSTERESIS_TEMP_C < MAX_DISCHARGE_TEMP_C));
        
        bool charge_fault_check = (past_error_reason.DETECT_MODE == CHARGER);

        datastore->TEMP_HYSTERESIS |= (charge_fault_check
                                    && isl_int_temp < MAX_CHARGE_TEMP_C
                                    && !(isl_int_temp + HYSTERESIS_TEMP_C < MAX_CHARGE_TEMP_C));

        datastore->TEMP_HYSTERESIS |= (charge_fault_check
                                    && thermistor_temp < MAX_CHARGE_TEMP_C
                                    && !(thermistor_temp + HYSTERESIS_TEMP_C < MAX_CHARGE_TEMP_C));
        
        
    }
}

////////////////////////////////////////////////////////////////////



void init(void){
//INIT STEPS
    
    I2C_ERROR_FLAGS = 0;
    
    /* Initialize the device */
    SYSTEM_Initialize();
    TMR4_StartTimer();   //Keep timer running
    DAC_SetOutput(0);   //Make sure DAC output is 0V = VSS
    
    /* Initialize LEDS */
    redLED = 1;  //Set high to default RED led to off
    greenLED = 1;  //Set high to default GREEN led to off
    blueLED = 1;  //Set high to default BLUE led to off
    TRISB3 = 0; // Set greenLED as output
    TRISA6 = 0; //Set blueLED as output
    TRISA7 = 0; //Set redLED as output
    ANSB3 = 0; //Set Green LED as digital. Red and Blue are always digital.

    /* Immediately start yellow LED so we know board is alive */
    Set_LED_RGB(0b110);

    //* Set up I2C pins */
    TRIS_SDA = 1;     //SDA - Make sure both pins are inputs
    TRIS_SCL = 1;     //SCL
    ANS_SDA = 0;    //Set both pins as digital
    ANS_SCL = 0;
    I2C1_Init();
    ClearI2CBus();  //Clear I2C bus once on startup just in case
    while (SDA == 0 || SCL == 0){   //If bus is still not idle (meaning pins aren't high), which shouldn't be possible, then keep trying to clear bus. Do not pass go. Do not collect $200.
        __debug_break();
        ClearI2CBus();
    }
    
    ISL_SetSpecificBits(ISL.ENABLE_DISCHARGE_FET, 0);       //Make sure the pack is turned off in case we had some weird reset
    ISL_SetSpecificBits(ISL.ENABLE_CHARGE_FET, 0);
    modelnum = checkModelNum();
    //INIT END
    
    state = IDLE;
}

void sleep(void){
    #ifndef __DEBUG     //It won't go to sleep but it will get stuck in SLEEP state with no way out.
    ISL_SetSpecificBits(ISL.SLEEP, 1);  
    #endif

}

void idle(void){
    Set_LED_RGB(0b010); //LED Green
    //ledBlinkpattern (4, 0b010, 500);
    
    
    if (detect == TRIGGER                       //Trigger is pulled
        && minCellOK()          //Min cell is not below low voltage cut out of 3V
        && ISL_GetSpecificBits_cached(ISL.WKUP_STATUS) //Make sure WKUP = 1 meaning charger connected or trigger pressed
        && full_discharge_flag == false             //Make sure pack hasn't just been fully discharged
        && safetyChecks()
        ){
            sleep_timeout_counter.enable = false;   //We aren't going to be sleeping soon
            state = OUTPUT_EN;
            //resetLEDBlinkPattern();
    }
    else if (detect == CHARGER                       //Charger is connected
            && charge_complete_flag == false         //We haven't already done a complete charge cycle
            && maxCellOK()          //Max cell < 4.20V
            && ISL_GetSpecificBits_cached(ISL.WKUP_STATUS) //Make sure WKUP = 1 meaning charger connected or trigger pressed
            && safetyChecks()
        ){
                sleep_timeout_counter.enable = false;   //We aren't going to be sleeping soon
                state = CHARGING; 
                //resetLEDBlinkPattern();
    }
    else if (detect == NONE                         //Start sleep counter if we are idle with no charger or trigger, but no errors
            && ISL_GetSpecificBits_cached(ISL.WKUP_STATUS) == 0
            && sleep_timeout_counter.enable == false
            && safetyChecks()
            ){
                sleep_timeout_counter.value = 0;
                sleep_timeout_counter.enable = true;
    }
    else if (!safetyChecks()){                        //Somehow there was an error  
        sleep_timeout_counter.enable = false;
        state = ERROR;
        //resetLEDBlinkPattern();
        //TODO: add checking for other error types. Make sure to add exit methods to error state function as well.
    }
    
    if ( (charge_complete_flag == true && cellstats.maxcell_mV < PACK_CHARGE_NOT_COMPLETE_THRESH_mV) || (detect != CHARGER)){      //If the max cell voltage is below 4100mV and the pack is marked as fully charged, unmark it as charged. Also, if removed from charger, clear charge complete flag.
        charge_complete_flag = false;
    }
    
    
    if (sleep_timeout_counter.value >  IDLE_SLEEP_TIMEOUT && sleep_timeout_counter.enable == true){    //938*32ms = 30.016s //If we are in IDLE state for 30 seconds and not on the charger, go to sleep. We will stay awake on the charger since we have power to spare and can then make sure battery voltages don't drop over time.
        sleep_timeout_counter.enable = false;
        sleep_timeout_counter.value = 0;
        #ifndef __DEBUG
        state = SLEEP;
        #endif
    }
    
    //TODO: add entry in to error state if trigger pulled while voltage too low so we go to sleep
            //add entry to error state if charger connected while voltage too high?
    
}

void charging(void){

    if (!ISL_GetSpecificBits_cached(ISL.ENABLE_CHARGE_FET)     //if we aren't already charging
        && detect == CHARGER
        && maxCellOK()
        && ISL_GetSpecificBits_cached(ISL.WKUP_STATUS)
        && safetyChecks()
        && chargeTempCheck()
        ){
            charge_duration_counter.value = 0;
            charge_duration_counter.enable = true;          //Start charge timer
            ISL_SetSpecificBits(ISL.ENABLE_CHARGE_FET, 1);  //Enable Charging
            full_discharge_flag = false;                    //Clear full discharge flag once we start charging
            Set_LED_RGB(0b111); //White LED
    }
    else if (ISL_GetSpecificBits_cached(ISL.ENABLE_CHARGE_FET)     //same as above but we are already charging and all conditions are good
        && detect == CHARGER
        && maxCellOK()
        && ISL_GetSpecificBits_cached(ISL.WKUP_STATUS)
        && safetyChecks()
        && chargeTempCheck()
            ){
        //do nothing
    }
    else if (!maxCellOK()){         //Target voltage reached
        ISL_SetSpecificBits(ISL.ENABLE_CHARGE_FET, 0); //Disable Charging
        charge_duration_counter.enable = false;         //Stop charge timer
        if (charge_duration_counter.value < CHARGE_COMPELTE_TIMEOUT){   //313 * 32ms = 10.016s, if it took less than 10 seconds for max cell voltage to be > 4.20v, mark charge complete
            charge_complete_flag = true;
            state = IDLE;
        }
        else{       //Go to charge wait state and wait 70 seconds before starting next charge cycle
            state = CHARGING_WAIT;
        }
    }
    else if (!safetyChecks() || !chargeTempCheck()){     //There was an error
        ISL_SetSpecificBits(ISL.ENABLE_CHARGE_FET, 0); //Disable Charging
        charge_duration_counter.enable = false;         //Stop charge timer
        state = ERROR;
    }
    else{                                   //charger removed
        ISL_SetSpecificBits(ISL.ENABLE_CHARGE_FET, 0); //Disable Charging
        charge_duration_counter.enable = false;         //Stop charge timer
        state = IDLE;
    }
    
    
}

void chargingWait(void){
    Set_LED_RGB(0b001); //Blue LED
    if (!charge_wait_counter.enable){   //if counter isn't enabled, clear and enable it.
        charge_wait_counter.value = 0;
        charge_wait_counter.enable = true;  //Clear and start charge wait counter
    }
    else if (charge_wait_counter.value >= CHARGE_WAIT_TIMEOUT){         //2188 * 32ms = 70.016 seconds
        charge_wait_counter.enable = false;
        state = CHARGING;
    }
    
    if (detect == NONE){                    //Charger removed
        charge_wait_counter.enable = false;
        state = IDLE;
    }
    
    if (!safetyChecks()){  //Somehow there was an error
        charge_wait_counter.enable = false;
        state = ERROR;
    }
    
}

void cellBalance(void){
    
}

void outputEN(void){
        if (!ISL_GetSpecificBits_cached(ISL.ENABLE_DISCHARGE_FET)  //If discharge isn't already enabled
            && detect == TRIGGER                       //Trigger is pulled
            && ISL_GetSpecificBits_cached(ISL.WKUP_STATUS) //Make sure WKUP = 1 meaning charger connected or trigger pressed   
            && minCellOK()          //Min cell is not below low voltage cut out of 3V
            && safetyChecks()
                ){
                Set_LED_RGB(0b111); //White LED
                ISL_SetSpecificBits(ISL.ENABLE_DISCHARGE_FET, 1);
        }
        else if (ISL_GetSpecificBits_cached(ISL.ENABLE_DISCHARGE_FET)  //Same as above but we are already discharging and all conditions are good
            && detect == TRIGGER
            && ISL_GetSpecificBits_cached(ISL.WKUP_STATUS)
            && minCellOK()
            && safetyChecks()
                ){
                //do nothing
        }
        else if (!minCellOK()){                                 //If we hit the min cell voltage cut off, prevent discharging battery further until it is put on charger
            full_discharge_flag = true;
            ISL_SetSpecificBits(ISL.ENABLE_DISCHARGE_FET, 0);   //Disable discharging
            state = IDLE;
        }     
        else if (!safetyChecks()){
                ISL_SetSpecificBits(ISL.ENABLE_DISCHARGE_FET, 0);   //Disable discharging
                state = ERROR;
        }
        else {                                                  //Trigger released; WKUP status = 1
            ISL_SetSpecificBits(ISL.ENABLE_DISCHARGE_FET, 0);   //Disable discharging
            state = IDLE;
        }
}

void error(void){
    //TODO: Add LED indicator function that communicates past_error_reason
    //TODO: Add a write to EEPROM with error info for logging

    //Set_LED_RGB(0b100); //Red LED
    ISL_Write_Register(FETControl, 0b00000000);     //Make sure all FETs are disabled
    
    current_error_reason = (error_reason_t){0};
    setErrorReasonFlags(&current_error_reason);
    
    if (!current_error_reason.ISL_INT_OVERTEMP_FLAG
        && !current_error_reason.ISL_EXT_OVERTEMP_FLAG 
        && !current_error_reason.ISL_INT_OVERTEMP_PICREAD 
        && !current_error_reason.THERMISTOR_OVERTEMP_PICREAD 
        && !current_error_reason.CHARGE_OC_FLAG 
        && !current_error_reason.DISCHARGE_OC_FLAG 
        && !current_error_reason.DISCHARGE_SC_FLAG 
        && !current_error_reason.DISCHARGE_OC_SHUNT_PICREAD 
        && !current_error_reason.CHARGE_ISL_OVERTEMP_PICREAD 
        && !current_error_reason.CHARGE_THERMISTOR_OVERTEMP_PICREAD 
        && !current_error_reason.TEMP_HYSTERESIS 
        && current_error_reason.DETECT_MODE == NONE
        && discharge_current_mA == 0
            ){
            sleep_timeout_counter.enable = false;
            past_error_reason = (error_reason_t){0};    //Clear error reason value for future usage
            current_error_reason = (error_reason_t){0};
            resetLEDBlinkPattern();
            state = IDLE;
            return;
        }
    
    if (past_error_reason.ISL_INT_OVERTEMP_FLAG) ledBlinkpattern (4, 0b100, 500);
    else if (past_error_reason.ISL_EXT_OVERTEMP_FLAG) ledBlinkpattern (5, 0b100, 500);
    else if (past_error_reason.ISL_INT_OVERTEMP_PICREAD) ledBlinkpattern (6, 0b100, 500);
    else if (past_error_reason.THERMISTOR_OVERTEMP_PICREAD) ledBlinkpattern (7, 0b100, 500);
    else if (past_error_reason.CHARGE_OC_FLAG) ledBlinkpattern (8, 0b100, 500);
    else if (past_error_reason.DISCHARGE_OC_FLAG) ledBlinkpattern (9, 0b100, 500);
    else if (past_error_reason.DISCHARGE_SC_FLAG) ledBlinkpattern (10, 0b100, 500);
    else if (past_error_reason.DISCHARGE_OC_SHUNT_PICREAD) ledBlinkpattern (11, 0b100, 500);
    else if (past_error_reason.CHARGE_ISL_OVERTEMP_PICREAD) ledBlinkpattern (12, 0b100, 500);
    else if (past_error_reason.CHARGE_THERMISTOR_OVERTEMP_PICREAD) ledBlinkpattern (13, 0b100, 500);
    //else if (past_error_reason.TEMP_HYSTERESIS) ledBlinkpattern (14, 0b100, 500);
    //else if (past_error_reason.DETECT_MODE == CHARGER) ledBlinkpattern (2, 0b101, 500);                  //You still have the vacuum on the charger!
    //else if (past_error_reason.DETECT_MODE == TRIGGER) ledBlinkpattern (3, 0b101, 500);                  
    else ledBlinkpattern (20, 0b100, 500);                                                                  //Unidentified Error
    
    
    
    
    
    
    
    if (sleep_timeout_counter.enable == false){  //If there is an error, start sleep counter (if it isn't already started), so we sleep if in error state for too long
        sleep_timeout_counter.value = 0;
        sleep_timeout_counter.enable = true;
    }
    else if (sleep_timeout_counter.value >  ERROR_SLEEP_TIMEOUT && sleep_timeout_counter.enable == true){    //1876*32ms = 60.032s //If we are in ERROR state for 60 seconds, just go to sleep.
        sleep_timeout_counter.enable = false;
        //state = SLEEP;            //Commented out for debugging
        //TODO: Make sure it will actually sleep if trigger is still held down so WKUP STATUS == 1
    }
    
}

void main(void)
{
    init();
    
    while (1)
    {
        if (!ISL_GetSpecificBits(ISL.WKPOL)){//If somehow the ISL was reset and so WKPOL isn't correct, reinitialize everything and clear the I2C bus.
            __debug_break();
            I2C1_Init();
            ClearI2CBus();
            I2C_ERROR_FLAGS = 0;    //Clear error flags
        }
        
        ISL_ReadAllCellVoltages();
        ISL_calcCellStats();
        detect = checkDetect();
        isl_int_temp = ISL_GetInternalTemp();
        thermistor_temp = getThermistorTemp(modelnum);
        ISL_Read_Register(Config);      //Get config register so we can check WKUP status later on
        ISL_Read_Register(Status);      //Get Status register to check for error flags
        ISL_Read_Register(FETControl);  //Get current FET status
        discharge_current_mA = dischargeIsense_mA();
        
            //TODO: Add I2C error check here
        
        
        switch(state){
            case INIT:
                init();
                break;
                
            case SLEEP:
                sleep();
                break;
            
            case IDLE:
                idle();
                break;
                
            case CHARGING:
                charging();
                break;
                
            case CHARGING_WAIT:
                chargingWait();
                break;

            case CELL_BALANCE:
                cellBalance();
                break;
                
            case OUTPUT_EN:
                outputEN();
                break;

            case ERROR:
                error();
                break;
                
        }
        
        if (TMR4_HasOverflowOccured()){         //Every 32ms 
            if (charge_wait_counter.enable){
                charge_wait_counter.value++;
            }
            
            if (charge_duration_counter.enable){
                charge_duration_counter.value++;
            }
            
            if (sleep_timeout_counter.enable){
                sleep_timeout_counter.value++;
            }
            
            if (nonblocking_wait_counter.enable){
                nonblocking_wait_counter.value++;
            }
        
        
        }


        
        #ifdef __DEBUG
        for (uint8_t i = 0; i < __ISL_NUMBER_OF_REG; i++){
            ISL_Read_Register(i);
        }
        #endif



        
        
    }
}
