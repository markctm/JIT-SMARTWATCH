/****************************************************************************
 *   Aug 14 12:37:31 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
 ****************************************************************************/
 
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * 
 */
#include "config.h"
#include "jit_pairing.h"

#include "gui/mainbar/mainbar.h"
// #include "gui/mainbar/setup_tile/setup_tile.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/blectl.h"
#include "hardware/pmu.h"
#include "hardware/powermgm.h"
#include "hardware/motor.h" 
#include "gui/keyboard.h"
#include "hardware/wifictl.h"     

#include "hardware/powermgm.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include "ArduinoJson.h"

lv_obj_t *jit_pairing_tile=NULL;
lv_style_t jit_pairing_style;
lv_obj_t *login_btn = NULL;

TaskHandle_t _login_task =NULL;


lv_obj_t *jit_pairing_img = NULL;
lv_obj_t *jit_pairing_info_label = NULL;
lv_obj_t *jit_pairing_status_label=NULL;

lv_obj_t *jit_pairing_code_textfield = NULL;

LV_IMG_DECLARE(cancel_32px);
LV_IMG_DECLARE(update_64px);
LV_FONT_DECLARE(Ubuntu_16px);

static void jabil_pairing_num_textarea_event_cb( lv_obj_t * obj, lv_event_t event );
static void exit_jit_pairing_event_cb( lv_obj_t * obj, lv_event_t event );
bool jit_pairing_powermgm_loop_cb( EventBits_t event, void *arg ); 
static void login_btn_event_cb(lv_obj_t * obj, lv_event_t event);
bool jit_pairing_event_cb();

bool start_login_flag=false;
uint8_t login_state=LOGIN_CLEAN;
uint32_t jit_pairing_tile_num;
char token[10];

void login_task(void * pvParameters);
bool check_token();



void jit_pairing_tile_setup( void ) {
    // get an app tile and copy mainstyle
    jit_pairing_tile_num = mainbar_add_app_tile( 1, 1, "jit pairing" );
    jit_pairing_tile = mainbar_get_tile_obj( jit_pairing_tile_num );

    lv_style_copy( &jit_pairing_style, ws_get_setup_tile_style() );
    lv_style_set_text_font( &jit_pairing_style, LV_STATE_DEFAULT, &Ubuntu_16px);
    lv_obj_add_style( jit_pairing_tile, LV_OBJ_PART_MAIN, &jit_pairing_style );

   // lv_obj_t *jit_pairing_exit_btn = wf_add_image_button( jit_pairing_tile, cancel_32px, exit_jit_pairing_event_cb, &jit_pairing_style);
   // lv_obj_align( jit_pairing_exit_btn, jit_pairing_tile, LV_ALIGN_IN_TOP_LEFT, 10, 10 );

    jit_pairing_img = lv_img_create( jit_pairing_tile, NULL );
    lv_img_set_src( jit_pairing_img, &update_64px );
    lv_obj_align( jit_pairing_img, jit_pairing_tile, LV_ALIGN_IN_TOP_MID, 0, 10);

    jit_pairing_info_label = lv_label_create( jit_pairing_tile, NULL);
    lv_obj_add_style( jit_pairing_info_label, LV_OBJ_PART_MAIN, &jit_pairing_style  );
    lv_label_set_text( jit_pairing_info_label, "INSERT TOKEN");
    lv_obj_align( jit_pairing_info_label, jit_pairing_img, LV_ALIGN_IN_BOTTOM_MID, 0, 5 );

    jit_pairing_status_label = lv_label_create( jit_pairing_tile, NULL);
    lv_obj_add_style( jit_pairing_status_label, LV_OBJ_PART_MAIN, &jit_pairing_style  );
    lv_label_set_text( jit_pairing_status_label, "No Status");
    lv_obj_align( jit_pairing_status_label, jit_pairing_tile, LV_ALIGN_IN_BOTTOM_LEFT, 2, -20);


    lv_obj_t *jit_pairing_code_cont = lv_obj_create( jit_pairing_tile, NULL );
    lv_obj_set_size(jit_pairing_code_cont, lv_disp_get_hor_res( NULL ) / 2 , 35 );
    lv_obj_add_style( jit_pairing_code_cont, LV_OBJ_PART_MAIN, &jit_pairing_style  );
    lv_obj_align( jit_pairing_code_cont, jit_pairing_info_label, LV_ALIGN_OUT_BOTTOM_MID, -40, 35 );
  
    jit_pairing_code_textfield = lv_textarea_create( jit_pairing_code_cont, NULL);
    lv_textarea_set_text( jit_pairing_code_textfield, " " );
    lv_textarea_set_pwd_mode( jit_pairing_code_textfield, true);
    lv_textarea_set_accepted_chars( jit_pairing_code_textfield, "-.0123456789.");
    lv_textarea_set_one_line( jit_pairing_code_textfield, true);
    lv_textarea_set_cursor_hidden( jit_pairing_code_textfield, true);

    lv_obj_set_width( jit_pairing_code_textfield, lv_disp_get_hor_res( NULL ) / 2 );
    lv_obj_set_height( jit_pairing_code_textfield, lv_disp_get_ver_res( NULL ) / 2 );
    lv_obj_align( jit_pairing_code_textfield, jit_pairing_tile, LV_ALIGN_CENTER, -40, 50);

    //Add button for SPIFFS format
    login_btn = lv_btn_create( jit_pairing_tile, NULL);
    lv_obj_set_event_cb( login_btn, login_btn_event_cb );
    lv_obj_set_size( login_btn, 60, 35);
    lv_obj_add_style( login_btn, LV_BTN_PART_MAIN, ws_get_button_style() );
    lv_obj_align( login_btn, jit_pairing_code_textfield, LV_ALIGN_OUT_RIGHT_MID, 15, -40);
    lv_obj_t *login_btn_label = lv_label_create( login_btn, NULL );
    lv_label_set_text( login_btn_label, "LOGIN");

    lv_obj_set_event_cb( jit_pairing_code_textfield, jabil_pairing_num_textarea_event_cb );
    //blectl_register_cb( BLECTL_PIN_AUTH | BLECTL_PAIRING_SUCCESS | BLECTL_PAIRING_ABORT, jit_pairing_event_cb, "jit pairing" );
    powermgm_register_loop_cb( POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY | POWERMGM_WAKEUP, jit_pairing_powermgm_loop_cb, "jitsupport app loop" );
    jit_pairing_event_cb();

}

bool jit_pairing_event_cb() {
    
    statusbar_hide( true );
    powermgm_set_event( POWERMGM_WAKEUP_REQUEST );
    mainbar_jump_to_tilenumber( jit_pairing_tile_num, LV_ANIM_OFF );
    lv_label_set_text( jit_pairing_info_label, "INSERT TOKEN" );
    lv_obj_align( jit_pairing_info_label, jit_pairing_img, LV_ALIGN_OUT_BOTTOM_MID, 0, 5 );
    lv_obj_invalidate( lv_scr_act() );
    //motor_vibe(20);

    return( true );
} 

bool jit_pairing_powermgm_loop_cb( EventBits_t event, void *arg ) {

   TTGOClass *ttgo = TTGOClass::getWatch();

   if(event==POWERMGM_WAKEUP){

       switch(login_state){

           case(LOGIN_CLEAN):

            lv_textarea_set_text( jit_pairing_code_textfield," ");
            lv_label_set_text( jit_pairing_status_label, " " );
            login_state=LOGIN_SCREEN;

           break;

           case(LOGIN_SCREEN):

            if(pmu_is_vbus_plug() || (start_login_flag==true))
            {
                start_login_flag=true;
                jit_pairing_event_cb();
            }
            else
            {
                //Start Login Falso e cabo USB desplugado 
                //Volta para o estado Login Clean 
                start_login_flag=false;
                login_state=LOGIN_CLEAN;                               
            } 

           break;

           case(LOGIN_IN_PROCESS):
    
            start_login_flag=false;

           break;
       
           case(CHECK_FOR_LOGOUT):

                if(pmu_is_vbus_plug())
                {
                    //jit_pairing_event_cb();
                    login_state=LOGIN_CLEAN;
                }

           break;
    
        }

    }
    else
    {   
        login_state=LOGIN_CLEAN;
    }

return( true );
}


static void jabil_pairing_num_textarea_event_cb ( lv_obj_t * obj, lv_event_t event ) {
    if( event == LV_EVENT_CLICKED ) {

        char token[10];
        num_keyboard_set_textarea( obj );


        if(strcmp(token,"123456")==0)
        {          
            mainbar_jump_to_maintile( LV_ANIM_OFF );

        }

    }

}

static void exit_jit_pairing_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_to_maintile( LV_ANIM_OFF );
                                        break;
    }
}

static void login_btn_event_cb(lv_obj_t * obj, lv_event_t event){

    switch( event ) {
        case( LV_EVENT_CLICKED ): 

        strlcpy( token, lv_textarea_get_text( jit_pairing_code_textfield), sizeof(token) );
        log_i("Peguei o token: %s",token);

    //---- Task para Reestabelecimento da Conexão MQTT
        xTaskCreatePinnedToCore( login_task,                               /* Function to implement the task */
                                "login_Task",                                 /* Name of the task */
                                2000,                                         /* Stack size in words */
                                NULL,                                         /* Task input parameter */
                                2,                                            /* Priority of the task */
                                &_login_task,                              /* Task handle. */
                                0);      
        break;
    }

}


void login_task(void * pvParameters)
{

  log_i("Inicialização de Login ");

  while(1)
  {           
      lv_label_set_text( jit_pairing_status_label, "CHECKING TOKEN...");

      vTaskDelay(1000/ portTICK_PERIOD_MS );
      if(check_token())
      {
        // LOGIN SUCCESS 
        lv_label_set_text( jit_pairing_status_label, "OK READY TO GO... WELCOME !");          
        vTaskDelay(1000/ portTICK_PERIOD_MS );
        start_login_flag=false;
        login_state=CHECK_FOR_LOGOUT;
        mainbar_jump_to_maintile( LV_ANIM_OFF );

      }

    vTaskDelete(NULL);
  }

}

bool check_token(){
        
    if(wifictl_get_event( WIFICTL_CONNECT ))
    {
        
        
        HTTPClient http;
        char post[100];
        http.begin(LOGIN_API_URL);
        http.addHeader("Content-Type", "application/json");

        //---------- Create JSON POST-----------
        StaticJsonDocument<200> doc;
        doc["token"] = token;
    
        String postBody;
        serializeJson(doc, postBody);
        postBody.toCharArray(post,sizeof(post));
        //--------------------------------------


        int httpResponseCode = http.POST(postBody);

        if(httpResponseCode > 0) {
     
            if(httpResponseCode == HTTP_CODE_OK) {
                              
               // String payload = http.getString();
              } 
              else {
                  //log_i("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
                  lv_label_set_text( jit_pairing_status_label, "Login Error !");
                  http.end();
                  return false;
              }

        
          }
    }
    else
    {

        lv_label_set_text( jit_pairing_status_label, "No Wifi !");
        return false;

    }

return true;
          
}

