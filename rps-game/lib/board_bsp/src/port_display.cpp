#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include "port_display.h"
#include "epaper_config.h"

#define TAG "display"
#define BUFFER_SIZE (EPD_WIDTH * EPD_HEIGHT / 8)

static uint8_t *buffer = NULL;
static esp_lcd_panel_io_handle_t io_handle;

const uint8_t WF_Full_1IN54[159] =
{											
    0x80,0x48,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x40,0x48,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x80,0x48,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x40,0x48,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xA,
    0x0,0x0,0x0,0x0,0x0,0x0,0x8,0x1,0x0,0x8,0x1,0x0,0x2,				
    0xA,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x22,0x22,0x22,0x22,0x22,0x22,0x0,
    0x0,0x0,0x22,0x17,0x41,0x0,0x32,0x20
};

unsigned char WF_PARTIAL_1IN54[159] =
{
    0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x80,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x40,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0xF,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x1,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x22,0x22,0x22,0x22,0x22,0x22,0x0,0x0,0x0,0x02,
    0x17,0x41,0xB0,0x32,0x28,
};

void set_rst_1(){gpio_set_level(EPD_RST_PIN,1);}
void set_rst_0(){gpio_set_level(EPD_RST_PIN,0);}

void read_busy() {
    while(gpio_get_level(EPD_BUSY_PIN) == 1) {
        vTaskDelay(pdMS_TO_TICKS(5));   //LOW: idle, HIGH: busy
    }
}

void PortDisplay_Init() {
    buffer = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "Initialize SPI");
    esp_err_t ret;
  	spi_bus_config_t buscfg = {};
  	buscfg.miso_io_num = EPD_MISO_PIN;
  	buscfg.mosi_io_num = EPD_MOSI_PIN;
  	buscfg.sclk_io_num = EPD_SCK_PIN;
  	buscfg.quadwp_io_num = -1;
  	buscfg.quadhd_io_num = -1;
  	buscfg.max_transfer_sz = EPD_WIDTH * EPD_HEIGHT;
    ret = spi_bus_initialize((spi_host_device_t)EPD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO);
  	ESP_ERROR_CHECK(ret);

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = EPD_DC_PIN;
    io_config.cs_gpio_num = EPD_CS_PIN;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 2;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EPD_SPI_NUM, &io_config, &io_handle));

  	
    gpio_config_t gpio_conf = {};
	gpio_conf.intr_type = GPIO_INTR_DISABLE;
	gpio_conf.mode = GPIO_MODE_OUTPUT;
	gpio_conf.pin_bit_mask = (0x1ULL<<EPD_RST_PIN);
	gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
	ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

	gpio_conf.mode = GPIO_MODE_INPUT;
	gpio_conf.pin_bit_mask = (0x1ULL<<EPD_BUSY_PIN);
	ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    set_rst_1();
}

void EPD_SendData(uint8_t data) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, -1, &data, 1));
}

void EPD_SendCommand(uint8_t command) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, command, NULL, 0));
}

void writeBytes(uint8_t *buffer,int len) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_handle, -1, buffer, len));
}

void writeBytes(const uint8_t *buffer, int len) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_handle, -1, buffer, len));
}

void EPD_SetWindows(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend)
{
    EPD_SendCommand(0x44);  // SET_RAM_X_ADDRESS_START_END_POSITION
    EPD_SendData((Xstart>>3) & 0xFF);
    EPD_SendData((Xend>>3) & 0xFF);
	
    EPD_SendCommand(0x45);  // SET_RAM_Y_ADDRESS_START_END_POSITION
    EPD_SendData(Ystart & 0xFF);
    EPD_SendData((Ystart >> 8) & 0xFF);
    EPD_SendData(Yend & 0xFF);
    EPD_SendData((Yend >> 8) & 0xFF);
}

void EPD_SetCursor(uint16_t Xstart, uint16_t Ystart)
{
    EPD_SendCommand(0x4E); // SET_RAM_X_ADDRESS_COUNTER
    EPD_SendData(Xstart & 0xFF);

    EPD_SendCommand(0x4F); // SET_RAM_Y_ADDRESS_COUNTER
    EPD_SendData(Ystart & 0xFF);
    EPD_SendData((Ystart >> 8) & 0xFF);
}

void EPD_SetLut(const uint8_t *lut) {
	EPD_SendCommand(0x32);
    writeBytes(lut,153);
	read_busy();
	
    EPD_SendCommand(0x3f);
    EPD_SendData(lut[153]);
	
    EPD_SendCommand(0x03);
    EPD_SendData(lut[154]);
	
    EPD_SendCommand(0x04);
    EPD_SendData(lut[155]);
	EPD_SendData(lut[156]);
	EPD_SendData(lut[157]);

	EPD_SendCommand(0x2c);
    EPD_SendData(lut[158]);
}

void EPD_TurnOnDisplay() {
    EPD_SendCommand(0x22);
    EPD_SendData(0xc7);
	EPD_SendCommand(0x20);
    read_busy();
}

void EPD_TurnOnDisplayPart() {
    EPD_SendCommand(0x22);
    EPD_SendData(0xcf);
    EPD_SendCommand(0x20);
    read_busy();
}

void EPD_Init() {
    set_rst_1();
  	vTaskDelay(pdMS_TO_TICKS(50));
  	set_rst_0();
  	vTaskDelay(pdMS_TO_TICKS(20));
  	set_rst_1();
  	vTaskDelay(pdMS_TO_TICKS(50));

    read_busy();
    EPD_SendCommand(0x12);  //SWRESET
    read_busy();

    EPD_SendCommand(0x01); //Driver output control
    EPD_SendData(0xC7);
    EPD_SendData(0x00);
    EPD_SendData(0x01);

    EPD_SendCommand(0x11); //data entry mode
    EPD_SendData(0x01);

	EPD_SetWindows(0, EPD_WIDTH-1, EPD_HEIGHT-1, 0);

    EPD_SendCommand(0x3C); //BorderWavefrom
    EPD_SendData(0x01);

    EPD_SendCommand(0x18);
    EPD_SendData(0x80);

    EPD_SendCommand(0x22); //Load Temperature and waveform setting.
    EPD_SendData(0XB1);
    EPD_SendCommand(0x20);

    EPD_SetCursor(0, EPD_HEIGHT-1);
	read_busy();
	
	EPD_SetLut(WF_Full_1IN54);
}

void EPD_Clear() {
    memset(buffer,0xff,BUFFER_SIZE);
}

void EPD_Display() {
    EPD_SendCommand(0x24);
    assert(buffer);
    writeBytes(buffer,BUFFER_SIZE);
    EPD_TurnOnDisplay();
}

void EPD_DisplayPartBaseImage() {
    EPD_SendCommand(0x24);
    assert(buffer);
    writeBytes(buffer,BUFFER_SIZE);
    EPD_SendCommand(0x26);
    writeBytes(buffer,BUFFER_SIZE);
    EPD_TurnOnDisplay();
}

void EPD_Init_Partial() {
    set_rst_1();
  	vTaskDelay(pdMS_TO_TICKS(50));
  	set_rst_0();
  	vTaskDelay(pdMS_TO_TICKS(20));
  	set_rst_1();
  	vTaskDelay(pdMS_TO_TICKS(50));

	read_busy();
	
	EPD_SetLut(WF_PARTIAL_1IN54);

    EPD_SendCommand(0x37); 
    EPD_SendData(0x00);  
    EPD_SendData(0x00);  
    EPD_SendData(0x00);  
    EPD_SendData(0x00); 
    EPD_SendData(0x00);  	
    EPD_SendData(0x40);  
    EPD_SendData(0x00);  
    EPD_SendData(0x00);   
    EPD_SendData(0x00);  
    EPD_SendData(0x00);
	
    EPD_SendCommand(0x3C); //BorderWavefrom
    EPD_SendData(0x80);
	
	EPD_SendCommand(0x22); 
	EPD_SendData(0xc0); 
	EPD_SendCommand(0x20); 
	read_busy();
}

void EPD_DisplayPart() {
    EPD_SendCommand(0x24);
    assert(buffer);
    writeBytes(buffer,5000);
    EPD_TurnOnDisplayPart();
}

void EPD_DrawColorPixel(uint16_t x, uint16_t y, uint8_t color) {
  if (x >= EPD_WIDTH || y >= EPD_HEIGHT || buffer == NULL) {
    return;
  }

  uint16_t index = y * 25 + (x >> 3);  // 25 == 200/8
  uint8_t bit = 7 - (x & 0x07);
  if (color == DRIVER_COLOR_WHITE) {
    buffer[index] |= (0x01 << bit);
  } else {
    buffer[index] &= ~(0x01 << bit);
  }
}

void EPD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color) {
  for (uint16_t yy = y; yy < (uint16_t)(y + h); yy++) {
    for (uint16_t xx = x; xx < (uint16_t)(x + w); xx++) {
      EPD_DrawColorPixel(xx, yy, color);
    }
  }
}

void EPD_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint8_t color) {
  for (uint16_t xx = x; xx < (uint16_t)(x + w); xx++) {
    EPD_DrawColorPixel(xx, y, color);
  }
}

void EPD_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint8_t color) {
  for (uint16_t yy = y; yy < (uint16_t)(y + h); yy++) {
    EPD_DrawColorPixel(x, yy, color);
  }
}

void EPD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color) {
  if (w == 0 || h == 0) {
    return;
  }
  EPD_DrawHLine(x, y, w, color);
  EPD_DrawHLine(x, y + h - 1, w, color);
  EPD_DrawVLine(x, y, h, color);
  EPD_DrawVLine(x + w - 1, y, h, color);
}

void EPD_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color) {
  int16_t dx = abs(x1 - x0);
  int16_t sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0);
  int16_t sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  while (true) {
    if (x0 >= 0 && y0 >= 0) {
      EPD_DrawColorPixel((uint16_t)x0, (uint16_t)y0, color);
    }
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int16_t e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void EPD_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color) {
  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;
  EPD_DrawColorPixel(x0, y0 + r, color);
  EPD_DrawColorPixel(x0, y0 - r, color);
  EPD_DrawColorPixel(x0 + r, y0, color);
  EPD_DrawColorPixel(x0 - r, y0, color);
  while (x < y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;
    EPD_DrawColorPixel(x0 + x, y0 + y, color);
    EPD_DrawColorPixel(x0 - x, y0 + y, color);
    EPD_DrawColorPixel(x0 + x, y0 - y, color);
    EPD_DrawColorPixel(x0 - x, y0 - y, color);
    EPD_DrawColorPixel(x0 + y, y0 + x, color);
    EPD_DrawColorPixel(x0 - y, y0 + x, color);
    EPD_DrawColorPixel(x0 + y, y0 - x, color);
    EPD_DrawColorPixel(x0 - y, y0 - x, color);
  }
}

void EPD_FillCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color) {
  for (int16_t y = -r; y <= r; y++) {
    for (int16_t x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) {
        EPD_DrawColorPixel(x0 + x, y0 + y, color);
      }
    }
  }
}

void EPD_FlushFull(void) {
  EPD_Init();
  EPD_Display();
  EPD_Init_Partial();
}

void EPD_FlushPartial(void) {
  EPD_DisplayPart();
}

void EPD_BlitFullscreen(const uint8_t *bitmap) {
  assert(buffer);
  assert(bitmap);
  memcpy(buffer, bitmap, BUFFER_SIZE);
}

void PortLvgl_Start_Init() {
  PortDisplay_Init();
  EPD_Init();
  EPD_Clear();
  EPD_DisplayPartBaseImage();
  EPD_Init_Partial();
}