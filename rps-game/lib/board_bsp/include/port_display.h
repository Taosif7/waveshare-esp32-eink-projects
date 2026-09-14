#ifndef PORT_DISPLAY_H
#define PORT_DISPLAY_H


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DRIVER_COLOR_WHITE = 0xff,
  DRIVER_COLOR_BLACK = 0x00,
  FONT_BACKGROUND = DRIVER_COLOR_WHITE,
} COLOR_IMAGE;
void PortLvgl_Start_Init();
void PortDisplay_Init();
void EPD_Init();    /* 墨水屏初始化 */
void EPD_Clear();   /* 清空屏幕 */
void EPD_Display(); /* 刷buffer到墨水屏 */
/*局部刷新*/
void EPD_DisplayPartBaseImage();
void EPD_Init_Partial();
void EPD_DisplayPart();
void EPD_DrawColorPixel(uint16_t x, uint16_t y, uint8_t color);
void EPD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color);
void EPD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color);
void EPD_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint8_t color);
void EPD_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint8_t color);
void EPD_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
void EPD_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color);
void EPD_FillCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color);
void EPD_FlushFull(void);
void EPD_FlushPartial(void);
/** Copy a full-screen 200x200 1-bit packed bitmap (MSB-first, 1=white) into the FB. */
void EPD_BlitFullscreen(const uint8_t *bitmap);

#ifdef __cplusplus
}
#endif




#endif
