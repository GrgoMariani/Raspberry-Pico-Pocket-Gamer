#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "ili9341.h"

#define SPI_PORT spi0
#define PIN_CS   16
#define PIN_RST 17
#define PIN_DC 18
#define PIN_MOSI 7
#define PIN_SCK  6
#define PIN_LED 26
#define PIN_MISO 4

#define ILI9341_NOP 0x00     ///< No-op register
#define ILI9341_SWRESET 0x01 ///< Software reset register
#define ILI9341_RDDID 0x04   ///< Read display identification information
#define ILI9341_RDDST 0x09   ///< Read Display Status

#define ILI9341_SLPIN 0x10  ///< Enter Sleep Mode
#define ILI9341_SLPOUT 0x11 ///< Sleep Out
#define ILI9341_PTLON 0x12  ///< Partial Mode ON
#define ILI9341_NORON 0x13  ///< Normal Display Mode ON

#define ILI9341_RDMODE 0x0A     ///< Read Display Power Mode
#define ILI9341_RDMADCTL 0x0B   ///< Read Display MADCTL
#define ILI9341_RDPIXFMT 0x0C   ///< Read Display Pixel Format
#define ILI9341_RDIMGFMT 0x0D   ///< Read Display Image Format
#define ILI9341_RDSELFDIAG 0x0F ///< Read Display Self-Diagnostic Result

#define ILI9341_INVOFF 0x20   ///< Display Inversion OFF
#define ILI9341_INVON 0x21    ///< Display Inversion ON
#define ILI9341_GAMMASET 0x26 ///< Gamma Set
#define ILI9341_DISPOFF 0x28  ///< Display OFF
#define ILI9341_DISPON 0x29   ///< Display ON

#define ILI9341_CASET 0x2A ///< Column Address Set
#define ILI9341_PASET 0x2B ///< Page Address Set
#define ILI9341_RAMWR 0x2C ///< Memory Write
#define ILI9341_RAMRD 0x2E ///< Memory Read

#define ILI9341_PTLAR 0x30    ///< Partial Area
#define ILI9341_VSCRDEF 0x33  ///< Vertical Scrolling Definition
#define ILI9341_MADCTL 0x36   ///< Memory Access Control
#define ILI9341_VSCRSADD 0x37 ///< Vertical Scrolling Start Address
#define ILI9341_PIXFMT 0x3A   ///< COLMOD: Pixel Format Set

#define ILI9341_FRMCTR1 0xB1 ///< Frame Rate Control (In Normal Mode/Full Colors)
#define ILI9341_FRMCTR2 0xB2 ///< Frame Rate Control (In Idle Mode/8 colors)
#define ILI9341_FRMCTR3 0xB3 ///< Frame Rate control (In Partial Mode/Full Colors)
#define ILI9341_INVCTR 0xB4  ///< Display Inversion Control
#define ILI9341_DFUNCTR 0xB6 ///< Display Function Control

#define ILI9341_PWCTR1 0xC0 ///< Power Control 1
#define ILI9341_PWCTR2 0xC1 ///< Power Control 2
#define ILI9341_PWCTR3 0xC2 ///< Power Control 3
#define ILI9341_PWCTR4 0xC3 ///< Power Control 4
#define ILI9341_PWCTR5 0xC4 ///< Power Control 5
#define ILI9341_VMCTR1 0xC5 ///< VCOM Control 1
#define ILI9341_VMCTR2 0xC7 ///< VCOM Control 2

#define ILI9341_RDID1 0xDA ///< Read ID 1
#define ILI9341_RDID2 0xDB ///< Read ID 2
#define ILI9341_RDID3 0xDC ///< Read ID 3
#define ILI9341_RDID4 0xDD ///< Read ID 4

#define ILI9341_GMCTRP1 0xE0 ///< Positive Gamma Correction
#define ILI9341_GMCTRN1 0xE1 ///< Negative Gamma Correction
//#define ILI9341_PWCTR6     0xFC


static inline void cs_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);
    asm volatile("nop \n nop \n nop");
}

static inline void cs_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}

static inline void dc_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_DC, 0);
    asm volatile("nop \n nop \n nop");
}

static inline void dc_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_DC, 1);
    asm volatile("nop \n nop \n nop");
}

static void inline send_byte(uint8_t data)
{   
    cs_select();
    dc_deselect();
    spi_write_blocking(SPI_PORT, &data, 1);
    cs_deselect();
}

static void inline send_short(uint16_t data)
{
    cs_select();
    dc_deselect();

    uint8_t shortBuffer[2];

    shortBuffer[0] = (uint8_t) (data >> 8);
    shortBuffer[1] = (uint8_t) data;

    spi_write_blocking(SPI_PORT, shortBuffer, 2);
    cs_deselect();
}

static void inline send_command(uint8_t command)
{
    dc_select();
    cs_select();

    spi_write_blocking(SPI_PORT, &command, 1);

    dc_deselect();
    cs_deselect();
}

void ili9341_init_display() 
{
    cs_select();

    gpio_put(PIN_RST, 0);
    sleep_ms(500);
    gpio_put(PIN_RST, 1);
    sleep_ms(500);

    send_command(0xEF);
    send_byte(0x03);
    send_byte(0x80);
    send_byte(0x02);

    send_command(0xCF);
    send_byte(0x00);
    send_byte(0xC1);
    send_byte(0x30);

    send_command(0xED);
    send_byte(0x64);
    send_byte(0x03);
    send_byte(0x12);
    send_byte(0x81);
    
    send_command(0xE8);
    send_byte(0x85);
    send_byte(0x00);
    send_byte(0x78);

    send_command(0xCB);
    send_byte(0x39);
    send_byte(0x2C);
    send_byte(0x00);
    send_byte(0x34);
    send_byte(0x02);

    send_command(0xF7);
    send_byte(0x20);

    send_command(0xEA);
    send_byte(0x00);
    send_byte(0x00);

    send_command(0xC0);
    send_byte(0x23);
    
    send_command(0xC1);
    send_byte(0x10);

    send_command(0xC5);
    send_byte(0x3e);
    send_byte(0x28);

    send_command(0xC7);
    send_byte(0x86);

    send_command(0x36);
    send_byte(0x48);

    send_command(0x3A);
    send_byte(0x55);

    send_command(0xB1);
    send_byte(0x00);
    send_byte(0x1F); // 61 Hz
    //send_byte(10); // 119 Hz

    send_command(0xB6);
    send_byte(0x08);
    send_byte(0x82);
    send_byte(0x27);

    send_command(0xF2);
    send_byte(0x00);

    send_command(0x26);
    send_byte(0x01);

    send_command(0xE0);
    send_byte(0x0F);
    send_byte(0x31);
    send_byte(0x2B);
    send_byte(0x0C);
    send_byte(0x0E);
    send_byte(0x08);
    send_byte(0x4E);
    send_byte(0xF1);
    send_byte(0x37);
    send_byte(0x07);
    send_byte(0x10);
    send_byte(0x03);
    send_byte(0x0E);
    send_byte(0x09);
    send_byte(0x00);

    send_command(0xE1);
    send_byte(0x00);
    send_byte(0x0E);
    send_byte(0x14);
    send_byte(0x03);
    send_byte(0x11);
    send_byte(0x07);
    send_byte(0x31);
    send_byte(0xC1);
    send_byte(0x48);
    send_byte(0x08);
    send_byte(0x0F);
    send_byte(0x0C);
    send_byte(0x31);
    send_byte(0x36);
    send_byte(0x0F);

    send_command(0x11);

    sleep_ms(120);

    send_command(0x29);

    sleep_ms(120);

    send_command(0x13);
}

void ili9341_init_SPI()
{
    // set up the SPI interface.
    spi_init(SPI_PORT, 62500000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_init(PIN_DC);
    gpio_init(PIN_RST);
    gpio_init(PIN_LED);

    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_RST, 0);
    gpio_put(PIN_LED, 1);
}

void ili9341_init_drawing()
{
    send_command(0x2A);
    send_short(0);
    send_short(239);

    send_command(0x2B);
    send_short(0);
    send_short(319);

    sleep_ms(10);

    send_command(0x2C);

    cs_select();
    dc_deselect();
}

void ili9341_write_buffer(uint8_t * buffer, uint32_t size_of_buffer)
{
    spi_write_blocking(SPI_PORT, buffer, size_of_buffer);
}

