/******************************************************************************
* File Name: main.c
*
* Description: This is the source code for the PSoC 4 CapSense CSX Button
*              Tuning code example for ModusToolbox.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2020-2022, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
* Header file includes
*******************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include "cycfg.h"
#include "cycfg_capsense.h"

/*******************************************************************************
* Macros
*******************************************************************************/

/* EZI2C interrupt priority must be higher than CapSense interrupt.
 * Lower number means higher priority.
 */
#define EZI2C_INTR_PRIORITY         (2u)
#define CAPSENSE_INTR_PRIORITY      (3u)
#define NUMBER_OF_SLIDER_SEGMENTS   (6u)

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static cy_capsense_status_t initialize_capsense(void);
static cy_en_scb_ezi2c_status_t initialize_ezi2c(void);
static void capsense_isr(void);
static void ezi2c_isr(void);

#if CY_CAPSENSE_BIST_EN
static void measure_cp(void);
#endif

/*******************************************************************************
* Global Definitions
*******************************************************************************/
cy_stc_scb_ezi2c_context_t CYBSP_EZI2C_context;

/* Variables for Sensor Cp measurement */
#if CY_CAPSENSE_BIST_EN
uint32_t sensor_id;
uint32_t sense_cap[NUMBER_OF_SLIDER_SEGMENTS];
cy_en_capsense_bist_status_t measure_status[NUMBER_OF_SLIDER_SEGMENTS];
#endif

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  System entrance point. This function performs
*  1. Initial setup of device
*  2. Initialize CapSense
*  3. Initialize tuner communication
*  4. Scan touch input continuously and sync with the tuner.
*
*******************************************************************************/
int main(void)
{
    uint32_t status;
    cy_rslt_t result = CY_RSLT_SUCCESS;
    
    /* Initialize the device and board peripherals */
    result = cybsp_init();
    
    /* Board init failed. Stop program execution */
    CY_ASSERT(CY_RSLT_SUCCESS == result);
    
    /* Enable global interrupts */
    __enable_irq();
    
    /* Initialize EZI2C */
    status = initialize_ezi2c();

    /* Halt the CPU if EZI2C initialization failed */
    CY_ASSERT(CY_SCB_EZI2C_SUCCESS == status);
    
    /* Initialize CapSense */
    status = initialize_capsense();

    /* Halt the CPU if CapSense initialization failed */
    CY_ASSERT(CY_CAPSENSE_STATUS_SUCCESS == status);
    
    /* To avoid compiler warning*/
    (void) result;
    (void) status;
    
    /* Initiate first scan */
    Cy_CapSense_ScanAllWidgets(&cy_capsense_context);
    
    for (;;)
    {
        if (CY_CAPSENSE_NOT_BUSY == Cy_CapSense_IsBusy(&cy_capsense_context))
        {

            /* Process all widgets */
            Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);
            
            /* Establishes synchronized operation between the CapSense
             * middleware and the CapSense Tuner tool.
             */
            Cy_CapSense_RunTuner(&cy_capsense_context);
            
            #if CY_CAPSENSE_BIST_EN
            measure_cp(); /* Measure the sensor capacitance using BIST */
            #endif

            /* Start the next scan */
            Cy_CapSense_ScanAllWidgets(&cy_capsense_context);
        }
    }
}

/*******************************************************************************
* Function Name: initialize_capsense
********************************************************************************
* Summary:
*  This function initializes the CapSense and configures the CapSense
*  interrupt.
*
* Return:
*  CapSense initiliazition status
*
*******************************************************************************/
static cy_capsense_status_t initialize_capsense(void)
{
    cy_capsense_status_t status = CY_CAPSENSE_STATUS_SUCCESS;
    
    /* CapSense interrupt configuration */
    const cy_stc_sysint_t CapSense_interrupt_config =
    {
        .intrSrc = CYBSP_CSD_IRQ,
        .intrPriority = CAPSENSE_INTR_PRIORITY,
    };

    /* Capture the CSD HW block and initialize it to the default state. */
    status = Cy_CapSense_Init(&cy_capsense_context);
    
    if (CY_CAPSENSE_STATUS_SUCCESS == status)
    {

        /* Initialize CapSense interrupt */
        if (CY_SYSINT_SUCCESS == Cy_SysInt_Init(&CapSense_interrupt_config, capsense_isr))
        {
            NVIC_ClearPendingIRQ(CapSense_interrupt_config.intrSrc);
            NVIC_EnableIRQ(CapSense_interrupt_config.intrSrc);

            /* Initialize the CapSense firmware modules. */
            status = Cy_CapSense_Enable(&cy_capsense_context);
        }
    }
    
    return status;
}

/*******************************************************************************
* Function Name: capsense_isr
********************************************************************************
* Summary:
*  Wrapper function for handling interrupts from CapSense block.
*
*******************************************************************************/
static void capsense_isr(void)
{
    Cy_CapSense_InterruptHandler(CYBSP_CSD_HW, &cy_capsense_context);
}

/*******************************************************************************
 * Function Name: initialize_ezi2c
 ********************************************************************************
 * Summary:
*   This function initializes the EzI2C module to communicate
*   with the CapSense Tuner tool.
*
* Return:
*  EZI2C initiliazition status
*
 *******************************************************************************/
static cy_en_scb_ezi2c_status_t initialize_ezi2c(void)
{
    cy_en_scb_ezi2c_status_t status = CY_SCB_EZI2C_SUCCESS;
    
    /* EZI2C interrupt configuration */
    const cy_stc_sysint_t ezi2c_intr_config =
    {
        .intrSrc = CYBSP_EZI2C_IRQ,
        .intrPriority = EZI2C_INTR_PRIORITY,
    };
    
    /* Capture the SCB EZI2C HW block and initialize it to the default state. */
    status = Cy_SCB_EZI2C_Init(CYBSP_EZI2C_HW, &CYBSP_EZI2C_config, &CYBSP_EZI2C_context);
    
    if (CY_SCB_EZI2C_SUCCESS == status)
    {   
        /* Initialize EZI2C interrupt */
        if (CY_SYSINT_SUCCESS == Cy_SysInt_Init(&ezi2c_intr_config, ezi2c_isr))
        {
            NVIC_ClearPendingIRQ(ezi2c_intr_config.intrSrc);
            NVIC_EnableIRQ(ezi2c_intr_config.intrSrc);

            /*Set up communication and initialize data buffer to CapSense data structure
            * to use Tuner application.
            */
            Cy_SCB_EZI2C_SetBuffer1(CYBSP_EZI2C_HW, (uint8_t *) &cy_capsense_tuner,
                                    sizeof(cy_capsense_tuner), sizeof(cy_capsense_tuner),
                                    &CYBSP_EZI2C_context);

            /* Initialize the EzI2C firmware module */
            Cy_SCB_EZI2C_Enable(CYBSP_EZI2C_HW);
        }
    }

    return status;
}

/*******************************************************************************
* Function Name: ezi2c_isr
********************************************************************************
* Summary:
*  Wrapper function for handling interrupts from EZI2C block.
*
*******************************************************************************/
static void ezi2c_isr(void)
{
    Cy_SCB_EZI2C_Interrupt(CYBSP_EZI2C_HW, &CYBSP_EZI2C_context);
}

#if CY_CAPSENSE_BIST_EN

/*******************************************************************************
 * Function Name: measure_cp
 ********************************************************************************
 * Summary:
 * Measures the sensor capacitance to determine Sense clock frequency. The measured
 * sensor capacitance (Cp) values are stored in the array 'sense_cap[x]', where x
 * is the sensor number.
 *
 *******************************************************************************/
static void measure_cp(void)
{
    for(sensor_id = CY_CAPSENSE_LINEARSLIDER0_SNS0_ID; sensor_id < CY_CAPSENSE_NUM_SNS_VALUE; sensor_id++)
    {
        measure_status[sensor_id] = Cy_CapSense_MeasureCapacitanceSensor(
                                CY_CAPSENSE_LINEARSLIDER0_WDGT_ID, sensor_id,
                                &sense_cap[sensor_id], &cy_capsense_context);
    }
}
#endif

/* [] END OF FILE */
