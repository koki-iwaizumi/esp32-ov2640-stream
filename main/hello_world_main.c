#include <string.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <esp_http_server.h>
#include <esp_camera.h>

#include "mbedtls/base64.h"
#include "cJSON.h"

#include "sensor.h"
#include "sccb.h"

/********* io ***********/
#define CAM_PIN_PWDN	2
#define CAM_PIN_RESET	15
#define CAM_PIN_XCLK	27
#define CAM_PIN_SIOD	25
#define CAM_PIN_SIOC	23
#define CAM_PIN_VSYNC	22
#define CAM_PIN_HREF	26
#define CAM_PIN_PCLK	21
#define CAM_PIN_D9		19
#define CAM_PIN_D8		36
#define CAM_PIN_D7		18
#define CAM_PIN_D6		39
#define CAM_PIN_D5		5
#define CAM_PIN_D4		34
#define CAM_PIN_D3		32
#define CAM_PIN_D2		35

/********* wifi ***********/
#define EXAMPLE_WIFI_SSID "*"
#define EXAMPLE_WIFI_PASS "*"

static const char *TAG = "APP";
sensor_t* sensor = NULL;

static const uint8_t regs_0[][1] = {
	{0x05},
	{0x44},
	{0x50},
	{0x51},
	{0x52},
	{0x53},
	{0x54},
	{0x55},
	{0x56},
	{0x57},
	{0x5A},
	{0x5B},
	{0x5C},
	{0x7C},
	{0x7D},
	{0x86},
	{0x87},
	{0x8C},
	{0xC0},
	{0xC1},
	{0xC2},
	{0xC3},
	{0xD3},
	{0xDA},
	{0xE0},
	{0xED},
	{0xF0},
	{0xF7},
	{0xF8},
	{0xF9},
	{0xFA},
	{0xFB},
	{0xFC},
	{0xFD},
	{0xFE},
	{0xFF}
};

static const uint8_t regs_1[][1] = {
	{0x00},
	{0x03},
	{0x04},
	{0x08},
	{0x09},
	{0x0A},
	{0x0B},
	{0x0C},
	{0x10},
	{0x11},
	{0x12},
	{0x13},
	{0x14},
	{0x15},
	{0x17},
	{0x18},
	{0x19},
	{0x1A},
	{0x1C},
	{0x1D},
	{0x24},
	{0x25},
	{0x26},
	{0x2A},
	{0x2B},
	{0x2D},
	{0x2E},
	{0x2F},
	{0x32},
	{0x34},
	{0x45},
	{0x46},
	{0x47},
	{0x48},
	{0x49},
	{0x4B},
	{0x4E},
	{0x4F},
	{0x50},
	{0x5D},
	{0x5E},
	{0x5F},
	{0x60},
	{0x61},
	{0x62}
};

static camera_config_t camera_config = {
	.pin_pwdn  = CAM_PIN_PWDN,
	.pin_reset = CAM_PIN_RESET,
	.pin_xclk = CAM_PIN_XCLK,
	.pin_sscb_sda = CAM_PIN_SIOD,
	.pin_sscb_scl = CAM_PIN_SIOC,

	.pin_d7 = CAM_PIN_D9,
	.pin_d6 = CAM_PIN_D8,
	.pin_d5 = CAM_PIN_D7,
	.pin_d4 = CAM_PIN_D6,
	.pin_d3 = CAM_PIN_D5,
	.pin_d2 = CAM_PIN_D4,
	.pin_d1 = CAM_PIN_D3,
	.pin_d0 = CAM_PIN_D2,
	.pin_vsync = CAM_PIN_VSYNC,
	.pin_href = CAM_PIN_HREF,
	.pin_pclk = CAM_PIN_PCLK,

	.xclk_freq_hz = 6000000,
	.ledc_timer = LEDC_TIMER_0,
	.ledc_channel = LEDC_CHANNEL_0,

	.pixel_format = PIXFORMAT_JPEG,
	.frame_size = FRAMESIZE_SVGA, //FRAMESIZE_UXGA

	.jpeg_quality = 12,
	.fb_count = 1
};

esp_err_t index_get_handler(httpd_req_t *req)
{
	extern const unsigned char index_start[] asm("_binary_index_html_start");
	extern const unsigned char index_end[]   asm("_binary_index_html_end");
	const size_t index_size = (index_end - index_start);

	httpd_resp_send_chunk(req, (const char *)index_start, index_size);
	httpd_resp_sendstr_chunk(req, NULL);

	return ESP_OK;
}

esp_err_t image_get_handler(httpd_req_t *req)
{
	camera_fb_t *fb = esp_camera_fb_get();
	if( ! fb){
		ESP_LOGE(TAG, "Camera capture failed");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	esp_err_t res = httpd_resp_set_type(req, "image/jpeg") || httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*") || httpd_resp_send(req, (const char *)fb->buf, fb->len);
	esp_camera_fb_return(fb);

	return res;
}

esp_err_t config0_get_handler(httpd_req_t *req)
{
	esp_err_t res = SCCB_Write(sensor->slv_addr, 0xFF, 0);
	if(res != ESP_OK){
		ESP_LOGE(TAG, "Failed SCCB_Write - 0xFF 0");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	int data_json_size = 10 * 1000;
	char *data_json = calloc(data_json_size, sizeof(char));
	if(data_json == NULL){
		ESP_LOGE(TAG, "Failed to allocate frame buffer - data_json");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	
	int regs_0_num = sizeof regs_0 / sizeof regs_0[0];
	strcat(data_json, "{");
	for(int i = 0; i < regs_0_num; i++){
		char data_json_buf[255] = {'\0'};
		sprintf(data_json_buf, "\"%d\":\"%d\"", regs_0[i][0], SCCB_Read(sensor->slv_addr, regs_0[i][0]));
		strcat(data_json, data_json_buf);
		if(i != regs_0_num - 1) strcat(data_json, ",");
	}
	strcat(data_json, "}");

	ESP_LOGI(TAG, "data_json=%s", data_json);
	ESP_LOGI(TAG, "data_json length=%d", strlen((const char*)data_json));

	res = httpd_resp_set_type(req, "application/json") || httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*") || httpd_resp_send(req, (const char *)data_json, strlen((const char*)data_json));
	free(data_json);

	return res;
}

esp_err_t config1_get_handler(httpd_req_t *req)
{
	esp_err_t res = SCCB_Write(sensor->slv_addr, 0xFF, 1);
	if(res != ESP_OK){
		ESP_LOGE(TAG, "Failed SCCB_Write - 0xFF 1");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	int data_json_size = 10 * 1000;
	char *data_json = calloc(data_json_size, sizeof(char));
	if(data_json == NULL){
		ESP_LOGE(TAG, "Failed to allocate frame buffer - data_json");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	
	int regs_1_num = sizeof regs_1 / sizeof regs_1[0];
	strcat(data_json, "{");
	for(int i = 0; i < regs_1_num; i++){
		char data_json_buf[255] = {'\0'};
		sprintf(data_json_buf, "\"%d\":\"%d\"", regs_1[i][0], SCCB_Read(sensor->slv_addr, regs_1[i][0]));
		strcat(data_json, data_json_buf);
		if(i != regs_1_num - 1) strcat(data_json, ",");
	}
	strcat(data_json, "}");

	ESP_LOGI(TAG, "data_json=%s", data_json);
	ESP_LOGI(TAG, "data_json length=%d", strlen((const char*)data_json));

	res = httpd_resp_set_type(req, "application/json") || httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*") || httpd_resp_send(req, (const char *)data_json, strlen((const char*)data_json));
	free(data_json);

	return res;
}

esp_err_t write_get_handler(httpd_req_t *req)
{
	size_t buf_len = httpd_req_get_url_query_len(req) + 1;
	if(buf_len <= 1){
		ESP_LOGE(TAG, "Failed buf_len");
		httpd_resp_send_500(req);
		return ESP_FAIL;	
	}

	char* buf = malloc(buf_len);
	if(httpd_req_get_url_query_str(req, buf, buf_len) != ESP_OK){
		free(buf);
		ESP_LOGE(TAG, "Failed httpd_req_get_url_query_str");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	char bank[4], key[4], value[4];
	ESP_LOGI(TAG, "Found URL query => %s", buf);
	if(httpd_query_key_value(buf, "bank", bank, sizeof(bank)) != ESP_OK || httpd_query_key_value(buf, "key", key, sizeof(key)) != ESP_OK || httpd_query_key_value(buf, "value", value, sizeof(value)) != ESP_OK){
		free(buf);
		ESP_LOGE(TAG, "Failed httpd_query_key_value");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}
	free(buf);

	int key_i = atoi(key);
	int value_i = atoi(value);
	int bank_i = atoi(bank);

	ESP_LOGI(TAG, "Found URL query parameter => key=%s, value=%s, bank=%s", key, value, bank);
	ESP_LOGI(TAG, "Found URL query parameter => key=%d, value=%d, bank=%d", key_i, value_i, bank_i);

	if(SCCB_Write(sensor->slv_addr, 0xFF, bank_i) != ESP_OK || SCCB_Write(sensor->slv_addr, key_i, value_i) != ESP_OK){
		ESP_LOGE(TAG, "Failed SCCB_Write");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	const char *resp = "{\"status\":\"success\"}";
	esp_err_t res = httpd_resp_set_type(req, "application/json") || httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*") || httpd_resp_send(req, resp, strlen(resp));
	return res;
}

httpd_uri_t index_uri = {
	.uri = "/",
	.method = HTTP_GET,
	.handler = index_get_handler,
};

httpd_uri_t image_uri = {
	.uri = "/image",
	.method = HTTP_GET,
	.handler = image_get_handler,
};

httpd_uri_t config0_uri = {
	.uri = "/config0",
	.method = HTTP_GET,
	.handler = config0_get_handler,
};

httpd_uri_t config1_uri = {
	.uri = "/config1",
	.method = HTTP_GET,
	.handler = config1_get_handler,
};

httpd_uri_t write_uri = {
	.uri = "/write",
	.method = HTTP_GET,
	.handler = write_get_handler,
};

httpd_handle_t start_webserver(void)
{
	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
	if(httpd_start(&server, &config) == ESP_OK){
		ESP_LOGI(TAG, "Registering URI handlers");
		httpd_register_uri_handler(server, &index_uri);
		httpd_register_uri_handler(server, &image_uri);
		httpd_register_uri_handler(server, &config0_uri);
		httpd_register_uri_handler(server, &config1_uri);
		httpd_register_uri_handler(server, &write_uri);
		return server;
	}

	ESP_LOGI(TAG, "Error starting server!");
	return NULL;
}

void stop_webserver(httpd_handle_t server)
{
	httpd_stop(server);
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	httpd_handle_t *server = (httpd_handle_t *) ctx;

	switch(event->event_id){
		case SYSTEM_EVENT_STA_START:
			ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
			ESP_ERROR_CHECK(esp_wifi_connect());
			break;
		case SYSTEM_EVENT_STA_GOT_IP:
			ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
			ESP_LOGI(TAG, "Got IP: '%s'", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));

			if(*server == NULL){
				*server = start_webserver();
			}
			break;
		case SYSTEM_EVENT_STA_DISCONNECTED:
			ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
			ESP_ERROR_CHECK(esp_wifi_connect());

			if(*server){
				stop_webserver(*server);
				*server = NULL;
			}
			break;
		default:
			break;
	}
	return ESP_OK;
}

static void initialise_wifi(void *arg)
{
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, arg));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = EXAMPLE_WIFI_SSID,
			.password = EXAMPLE_WIFI_PASS,
		},
	};
	ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main()
{
	static httpd_handle_t server = NULL;
	ESP_ERROR_CHECK(nvs_flash_init());

	esp_err_t err = esp_camera_init(&camera_config);
	if(err != ESP_OK){
		ESP_LOGE(TAG, "Camera Init Failed");
		return;
	}

	sensor = esp_camera_sensor_get();

	vTaskDelay(1000 / portTICK_RATE_MS);
	initialise_wifi(&server);
}
