/**
 ****************************************************************************************
 *
 * @file user_reset_mechanism.c
 *
 * @brief Reset Mechanism SW Example source code.
 *
 * Copyright (c) 2015-2020 Dialog Semiconductor. All rights reserved.
 *
 * This software ("Software") is owned by Dialog Semiconductor.
 *
 * By using this Software you agree that Dialog Semiconductor retains all
 * intellectual property and proprietary rights in and to this Software and any
 * use, reproduction, disclosure or distribution of the Software without express
 * written permission or a license agreement from Dialog Semiconductor is
 * strictly prohibited. This Software is solely for use on or in conjunction
 * with Dialog Semiconductor products.
 *
 * EXCEPT AS OTHERWISE PROVIDED IN A LICENSE AGREEMENT BETWEEN THE PARTIES, THE
 * SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. EXCEPT AS OTHERWISE
 * PROVIDED IN A LICENSE AGREEMENT BETWEEN THE PARTIES, IN NO EVENT SHALL
 * DIALOG SEMICONDUCTOR BE LIABLE FOR ANY DIRECT, SPECIAL, INDIRECT, INCIDENTAL,
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THE SOFTWARE.
 *
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @addtogroup APP
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "rwip_config.h"             // SW configuration
#include "gap.h"
#include "app_easy_timer.h"
#include "user_reset_mechanism.h"
#include "user_custs1_impl.h"
#include "user_custs1_def.h"
#include "co_bt.h"
#include "arch_console.h"

/*
 * TYPE DEFINITIONS
 ****************************************************************************************
 */

/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */

uint8_t app_connection_idx                      	__SECTION_ZERO("retention_mem_area0");
timer_hnd app_param_update_request_timer_used   	__SECTION_ZERO("retention_mem_area0");

/* 
 * Holds the latest reset status of the device
 */
uint16_t latest_reset_status 						__SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY

/* 
 * Holds the flags that indicate that either the NMI or the Hardfault handler has occured
 * the flags should be kept in an un-initialized area in order not to be wiped out by the
 * scatterload procedure when rebooting and downloading fresh fw
 */
uint8_t latest_fault_status[9]                      __SECTION_ZERO("retention_mem_area_uninit");
uint8_t fault_status                                __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
*/

/**
 ****************************************************************************************
 * @brief Notifies application that system has been reset.
 * @param[in] reset_stat The value of @c RESET_STAT_REG; if reset_stat is zero,
 *                       system has woken up from hibernation.
 * @note Application shall define this function or get notified when a reset occurs.
 ****************************************************************************************
 */
void reset_indication(uint16_t reset_stat)
{	
#if defined (__DA14531__)
	latest_reset_status = reset_stat ;
#else
    // The reset value of the POR_TIMER_REG is not retained only when POR occurs 
    if(reset_stat != 0x18)
        latest_reset_status = PORESET_VAL;
    else
        // The DA14585 cannot tell the difference between a HW or a SW reset, so its either POR or a RESET
        latest_reset_status = SWRESET_VAL; 
#endif
    latest_reset_status |= (uint16_t)latest_fault_status[FAULT_INDEX] << 8;
}

void user_set_hardfault_flag(void)
{
    latest_fault_status[FAULT_INDEX] |= HARDFAULT_OCCURED;
}

void user_set_watchdog_flag(void)
{
    latest_fault_status[FAULT_INDEX] |= NMI_OCCURED;
}

/**
 ****************************************************************************************
 * @brief Check if the we can trust the un-initialized area for determine the reset reason
 * @return void
 ****************************************************************************************
*/
void user_error_check(void)
{
    // Read the magic values from the un-initialized area
    volatile uint32_t magic_num_1 = co_read32p(&latest_fault_status[MAGIC_NUM_1_INDEX]);
    volatile uint32_t magic_num_2 = co_read32p(&latest_fault_status[MAGIC_NUM_2_INDEX]);
    
    // Check if the magic values are valid
    if (magic_num_1 == MAGIC_NUM_1 && magic_num_2 == MAGIC_NUM_2)
    {
        fault_status = latest_fault_status[FAULT_INDEX];
    }
    else
    {
        co_write32p(&latest_fault_status[MAGIC_NUM_1_INDEX], MAGIC_NUM_1);
        co_write32p(&latest_fault_status[MAGIC_NUM_2_INDEX], MAGIC_NUM_2);
    }
   /* 
    * The latest status is kept in the fault status variable,
    * re-initialize the value changed in the hardfault or NMI
    */
    latest_fault_status[FAULT_INDEX] = INVALID_FAULT;
}

/**
 ****************************************************************************************
 * @brief Handles the sourse of the reset and indicate the application
 * @return void
 ****************************************************************************************
*/
void user_reset_indication(void)
{
    volatile uint8_t reset = latest_reset_status & 0xFF;
    volatile uint8_t fault = (latest_reset_status >> 8);
    
	switch(reset)
	{
		case WDOGRESET_VAL:	
			arch_printf( "\n\r****** HW WDOG RESET ******\n\n\r" ) ;		
		break;
		
		case SWRESET_VAL:
        {
			arch_printf( "\n\r****** SOFTWARE RESET ******\n\n\r" ) ;
            /*
            * For the DA14585 the SW reset might occur either from the NMI or Hardfault
            * After hardfault the watchdog is reloaded in order to force an NMI which in
            * turn asserts the SW reset in the SYS_CTRL_REG
            */
            if(fault & HARDFAULT_OCCURED)
                arch_printf("\n\r****** SW RESET DUE TO HARDFAULT ******\n\r");
            else if (fault & NMI_OCCURED)
                arch_printf("\n\r****** SW RESET DUE TO NMI WATCHDOG ******\n\r");
        }
		break;
		
		case HWRESET_VAL:
			arch_printf( "\n\r****** HARDWARE RESET ******\n\n\r" ) ;
		break;
		
		case PORESET_VAL:
			arch_printf( "\n\r****** POWER ON RESET ******\n\n\r" ) ;
		break;

		default:
			arch_printf( "\n\r****** NO RESET ******\n\n\r" ) ;
		break;
    }
	
	arch_printf("RESET_STAT_REG = 0x%04x\n\n\r" , latest_reset_status ) ;
}

void user_app_init(void)
{
    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
    
    user_error_check();
    
#if defined (CFG_PRINTF)    
	user_reset_indication();
#endif			
    default_app_on_init();
}

/**
 ****************************************************************************************
 * @brief Parameter update request timer callback function.
 * @return void
 ****************************************************************************************
*/
static void param_update_request_timer_cb()
{
    app_easy_gap_param_update_start(app_connection_idx);
    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
}

static void update_db_reset_status(uint8_t conidx)
{
    struct custs1_val_set_req *req = KE_MSG_ALLOC(CUSTS1_VAL_SET_REQ, prf_get_task_from_id(TASK_ID_CUSTS1), TASK_APP, custs1_val_set_req);
    req->conidx = conidx;
    req->handle = SVC1_IDX_RESET_SOURCE_VAL;
    req->length = DEF_SVC1_RESET_SOURCE_CHAR_LEN;
    memcpy(req->value, &latest_reset_status, req->length);
    
    ke_msg_send(req);
}

void user_app_adv_start(void)
{
    app_easy_gap_undirected_advertise_start();
}

void user_app_connection(uint8_t connection_idx, struct gapc_connection_req_ind const *param)
{
    if (app_env[connection_idx].conidx != GAP_INVALID_CONIDX)
    {
        app_connection_idx = connection_idx;

        // Update the database with the new reset value
        update_db_reset_status(connection_idx);
        
        // Check if the parameters of the established connection are the preferred ones.
        // If not then schedule a connection parameter update request.
        if ((param->con_interval < user_connection_param_conf.intv_min) ||
            (param->con_interval > user_connection_param_conf.intv_max) ||
            (param->con_latency != user_connection_param_conf.latency) ||
            (param->sup_to != user_connection_param_conf.time_out))
        {
            // Connection params are not these that we expect
            app_param_update_request_timer_used = app_easy_timer(APP_PARAM_UPDATE_REQUEST_TO, param_update_request_timer_cb);
        }
    }
    else
    {
        // No connection has been established, restart advertising
        user_app_adv_start();
    }

    default_app_on_connection(connection_idx, param);
}

void user_app_adv_undirect_complete(uint8_t status)
{
    // If advertising was canceled then update advertising data and start advertising again
    if (status == GAP_ERR_CANCELED)
    {
        user_app_adv_start();
    }
}

void user_app_disconnect(struct gapc_disconnect_ind const *param)
{
    // Cancel the parameter update request timer
    if (app_param_update_request_timer_used != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(app_param_update_request_timer_used);
        app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
    }

    // Restart Advertising
    user_app_adv_start();
}

void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id)
{
    switch(msgid)
    {
        case CUSTS1_VAL_WRITE_IND:
        {
            struct custs1_val_write_ind const *msg_param = (struct custs1_val_write_ind const *)(param);

            switch (msg_param->handle)
            {
                case SVC1_IDX_CONTROL_POINT_VAL:
                    user_svc1_ctrl_wr_ind_handler(msgid, msg_param, dest_id, src_id);
                    break;

                default:
                    break;
            }
        } break;
        
        case GAPC_PARAM_UPDATED_IND:
        {
            // Cast the "param" pointer to the appropriate message structure
            struct gapc_param_updated_ind const *msg_param = (struct gapc_param_updated_ind const *)(param);

            // Check if updated Conn Params filled to preferred ones
            if ((msg_param->con_interval >= user_connection_param_conf.intv_min) &&
                (msg_param->con_interval <= user_connection_param_conf.intv_max) &&
                (msg_param->con_latency == user_connection_param_conf.latency) &&
                (msg_param->sup_to == user_connection_param_conf.time_out))
            {
            }
        } break;

        default:
            break;
    }
}

/// @} APP
