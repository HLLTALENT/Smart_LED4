#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"

#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_smartconfig.h"
/*  user include */
#include "Smartconfig.h"
#include "esp_log.h"
#include "Led.h"
#include "Bluetooth.h"
#include "Json_parse.h"
#include "tcp_bsp.h"
#include "Http.h"

#define TAG "User_Wifi" //打印的tag

TaskHandle_t my_tcp_connect_Handle;
EventGroupHandle_t wifi_event_group;
EventGroupHandle_t tcp_event_group;

wifi_config_t s_staconf;

uint8_t wifi_connect_sta = connect_N;
uint8_t wifi_work_sta = turn_on;
uint8_t start_AP = 0;
uint8_t bl_flag = 0; //蓝牙配网模式
uint8_t Wifi_ErrCode = 0;
uint16_t Net_ErrCode = 0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        //esp_wifi_connect();
        break;

    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        //Led_Status = LED_STA_WORK; //联网工作
        wifi_connect_sta = connect_Y;
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:

        ESP_LOGI(TAG, "断网");

        Wifi_ErrCode = event->event_info.disconnected.reason;
        if (Wifi_ErrCode >= 1 && Wifi_ErrCode <= 24) //适配APP，
        {
            Wifi_ErrCode += 300;
        }

        wifi_connect_sta = connect_N;
        if (start_AP == 1 || bl_flag == 1) //判断是不是要进入配网模式
        {
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        }
        else
        {
            //Led_Status = LED_STA_WIFIERR; //断网
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            esp_wifi_connect();
        }
        break;

    case SYSTEM_EVENT_AP_STACONNECTED: //AP模式-有STA连接成功
        //作为ap，有sta连接
        ESP_LOGI(TAG, "station:" MACSTR " join,AID=%d\n",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
        xEventGroupSetBits(tcp_event_group, AP_STACONNECTED_BIT);
        break;

    case SYSTEM_EVENT_AP_STADISCONNECTED: //AP模式-有STA断线
        ESP_LOGI(TAG, "station:" MACSTR "leave,AID=%d\n",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);

        g_rxtx_need_restart = true;
        xEventGroupClearBits(tcp_event_group, AP_STACONNECTED_BIT);
        break;

    default:
        ESP_LOGI(TAG, "event->event_id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

void initialise_wifi(char *wifi_ssid, char *wifi_password)
{
    printf("WIFI Reconnect,SSID=%s,PWD=%s\r\n", wifi_ssid, wifi_password);

    ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &s_staconf));
    if (s_staconf.sta.ssid[0] == '\0')
    {
        //ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &s_staconf));
        strcpy((char *)s_staconf.sta.ssid, wifi_ssid);
        strcpy((char *)s_staconf.sta.password, wifi_password);

        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &s_staconf));
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_connect();
    }
    else
    {
        ESP_ERROR_CHECK(esp_wifi_stop());
        memset(&s_staconf.sta, 0, sizeof(s_staconf));
        //printf("WIFI CHANGE\r\n");
        strcpy((char *)s_staconf.sta.ssid, wifi_ssid);
        strcpy((char *)s_staconf.sta.password, wifi_password);
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &s_staconf));
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_connect();
    }
    /*else if (strcmp(wifi_ssid, s_staconf.sta.ssid) == 0 && strcmp(wifi_password, s_staconf.sta.password) == 0)
    {

        if (wifi_con_sta == connect_Y)
        {
            printf("ALREADY CONNECT \r\n");
        }
        else
        {
            printf("WIFI NO CHANGE\r\n");
            ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &s_staconf));
            ESP_ERROR_CHECK(esp_wifi_start());
            esp_wifi_connect();
        }
    }
    else if (strcmp(wifi_ssid, s_staconf.sta.ssid) != 0 || strcmp(wifi_password, s_staconf.sta.password) != 0)
    {
        ESP_ERROR_CHECK(esp_wifi_stop());
        memset(&s_staconf.sta, 0, sizeof(s_staconf));
        printf("WIFI CHANGE\r\n");
        strcpy(s_staconf.sta.ssid, wifi_ssid);
        strcpy(s_staconf.sta.password, wifi_password);
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &s_staconf));
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_connect();
    }*/
}

void reconnect_wifi_usr(void)
{
    printf("WIFI Reconnect\r\n");
    ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &s_staconf));

    ESP_ERROR_CHECK(esp_wifi_stop());
    memset(&s_staconf.sta, 0, sizeof(s_staconf));

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &s_staconf));
    start_user_wifi();
}

void init_wifi(void) //
{
    start_AP = 0;
    tcpip_adapter_init();

    memset(&s_staconf.sta, 0, sizeof(s_staconf));
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); //实验，测试解决wifi中断问题
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &s_staconf));
    if (s_staconf.sta.ssid[0] != '\0')
    {
        printf("wifi_init_sta finished.");
        printf("connect to ap SSID:%s password:%s\r\n",
               s_staconf.sta.ssid, s_staconf.sta.password);

        bzero(wifi_data.wifi_ssid, sizeof(wifi_data.wifi_ssid));
        strcpy(wifi_data.wifi_ssid, (char *)s_staconf.sta.ssid);

        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &s_staconf));
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_connect();
        //Led_Status = LED_STA_TOUCH;
    }
    else
    {
        // printf("Waiting for SetupWifi ....\r\n");
        // wifi_init_softap();
        // my_tcp_connect();
    }
}

/*void stop_user_wifi(void)
{
    if (wifi_work_sta == turn_on)
    {
        ESP_ERROR_CHECK(esp_wifi_stop());
        wifi_work_sta = turn_off;
        printf("turn off WIFI! \n");
    }
}

void start_user_wifi(void)
{
    if (wifi_work_sta == turn_off)
    {
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_connect();

        wifi_work_sta = turn_on;
        printf("turn on WIFI! \n");
    }
}*/