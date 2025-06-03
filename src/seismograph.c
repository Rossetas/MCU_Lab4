/*
 * seismograph.c
 * 
 * Copyright 2025 marco <marco@Ubuntu-VM>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */

/* bibliotecas */
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/spi.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include "clock.h"
#include "console.h"
#include <math.h>
#include "sdram.h"
#include "lcd-spi.h"
#include "gfx.h"
/////////////////////////////////////////

/* MACROS */
#define GYR_RNW			(1 << 7) /* Write when zero  (ahorita en 1, read)*/  
#define GYR_MNS			(1 << 6) /* Multiple reads when 1 */
#define GYR_WHO_AM_I		0x0F
#define GYR_OUT_TEMP		0x26
#define GYR_STATUS_REG		0x27
#define GYR_CTRL_REG1		0x20
#define GYR_CTRL_REG1_PD	(1 << 3)
#define GYR_CTRL_REG1_XEN	(1 << 1)
#define GYR_CTRL_REG1_YEN	(1 << 0)
#define GYR_CTRL_REG1_ZEN	(1 << 2)
#define GYR_CTRL_REG1_BW_SHIFT	4
#define GYR_CTRL_REG4		0x23
#define GYR_CTRL_REG4_FS_SHIFT	4

#define GYR_OUT_X_L		0x28
#define GYR_OUT_X_H		0x29
#define GYR_OUT_Y_L		0x2A
#define GYR_OUT_Y_H		0x2B
#define GYR_OUT_Z_L		0x2C
#define GYR_OUT_Z_H		0x2D

/* --- Sensibilidad del giroscopio (dps/digit) para 500 dps --- */
#define L3GD20_SENSITIVITY_500DPS 0.0175f
/////////////////////////////////////////   

/* global Flags */
bool EN_comms = false;
bool warning_BAT = false;
///////////////////////////////////////// 

/* function prototype */
static void seismograph_setup(void);
static void spi_setup(void);
static void button_setup(void);
static void led_setup(void);
static void adc_setup(void);
static uint16_t read_adc_naiive(uint8_t channel);
static void seismograph(void);
/////////////////////////////////////////

static void spi_setup(void) // libopencm3/libopencm3-examples/examples/stm32/f3/stm32f3-discovery/spi
{   
    // clock
    rcc_periph_clock_enable(RCC_SPI5);
    rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOF);
	
    // GPIO
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO1);
    gpio_set(GPIOC, GPIO1);
    gpio_mode_setup(GPIOF, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO7 | GPIO8 | GPIO9);   
	gpio_set_af(GPIOF, GPIO_AF5, GPIO7 | GPIO8 | GPIO9);

    //spi initialization;
    spi_set_master_mode(SPI5);
	spi_set_baudrate_prescaler(SPI5, SPI_CR1_BR_FPCLK_DIV_64);
	spi_set_clock_polarity_0(SPI5);
	spi_set_clock_phase_0(SPI5);
	spi_set_full_duplex_mode(SPI5);
	spi_set_unidirectional_mode(SPI5); /* bidirectional but in 3-wire */
	spi_enable_software_slave_management(SPI5);
	spi_send_msb_first(SPI5);
	spi_set_nss_high(SPI5);
    SPI_I2SCFGR(SPI5) &= ~SPI_I2SCFGR_I2SMOD;
	spi_enable(SPI5);
}

static void button_setup(void)
{
	/* Enable GPIOA clock. */
	rcc_periph_clock_enable(RCC_GPIOA);

	/* Set GPIO0 (in GPIO port A) to 'input open-drain'. */
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO0);
}

static void led_setup(void)
{
	/* Enable GPIOG clock. */
	rcc_periph_clock_enable(RCC_GPIOG);

	gpio_mode_setup(GPIOG, GPIO_MODE_OUTPUT,
			GPIO_PUPD_NONE, GPIO13);
	
	// battery led
	gpio_mode_setup(GPIOG, GPIO_MODE_OUTPUT,
			GPIO_PUPD_NONE, GPIO14);

	/* Set GPIO13-14 (in GPIO port G) to 'output push-pull'. */
	//gpio_mode_setup(GPIOG, GPIO_MODE_OUTPUT,
			//GPIO_PUPD_NONE, GPIO13 | GPIO14);
}

static void adc_setup(void)
{
	/* And ADC*/
	rcc_periph_clock_enable(RCC_ADC1);

	/*****************************************************************/
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO2);
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO6);
	/*****************************************************************/

	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_3CYC);

	adc_power_on(ADC1);

}

static uint16_t read_adc_naiive(uint8_t channel)
{
	uint8_t channel_array[16];
	channel_array[0] = channel;
	adc_set_regular_sequence(ADC1, 1, channel_array);
	adc_start_conversion_regular(ADC1);
	while (!adc_eoc(ADC1));
	uint16_t reg16 = adc_read_regular(ADC1);
	return reg16;
}

static void seismograph_setup(void)
{
	clock_setup(); /* initialize our clock */
	console_setup(115200);

	spi_setup();
	button_setup();
	led_setup();
	adc_setup();
	/////////////////////////////////////////
    gpio_clear(GPIOC, GPIO1);
	spi_send(SPI5, GYR_CTRL_REG1); 
	spi_read(SPI5);
	spi_send(SPI5, GYR_CTRL_REG1_PD | GYR_CTRL_REG1_XEN |
			GYR_CTRL_REG1_YEN | GYR_CTRL_REG1_ZEN |
			(3 << GYR_CTRL_REG1_BW_SHIFT));
	spi_read(SPI5);
	gpio_set(GPIOC, GPIO1); 

    gpio_clear(GPIOC, GPIO1);
	spi_send(SPI5, GYR_CTRL_REG4);
	spi_read(SPI5);
	spi_send(SPI5, (1 << GYR_CTRL_REG4_FS_SHIFT));
	spi_read(SPI5);
	gpio_set(GPIOC, GPIO1);
}

int main(void) /********** MAIN **********/
{
	seismograph_setup();
	sdram_init();
	lcd_spi_init();
	msleep(2000);

	/******************************************************************/
	/******************************************************************/
	/******************************************************************/
	
	// Inicializa la biblioteca grafica con resolucion de 240x320
	gfx_init(lcd_draw_pixel, 240, 320);
	gfx_fillScreen(LCD_BLACK);

	// Titulo principal
	gfx_setTextColor(LCD_WHITE, LCD_BLACK);
	gfx_setTextSize(2);
	gfx_setCursor(30, 40);
	gfx_puts("seismograph");

	// Subtitulo
	gfx_setTextSize(3);
	gfx_setCursor(30, 70);
	gfx_puts("UCR");

	// Descripcion del sistema
	gfx_setTextSize(2);
	gfx_setCursor(15, 130);
	gfx_puts("Visualizacion");

	// colores
	gfx_setTextSize(2);
	gfx_setTextColor(LCD_RED, LCD_BLACK);
	gfx_setCursor(30, 160);
	gfx_puts("X - Rojo");

	gfx_setTextColor(LCD_BLUE, LCD_BLACK);
	gfx_setCursor(30, 185);
	gfx_puts("Y - Azul");

	gfx_setTextColor(LCD_GREEN, LCD_BLACK);
	gfx_setCursor(30, 210);
	gfx_puts("Z - Verde");

	gfx_setTextColor(LCD_WHITE, LCD_BLACK);
	gfx_setTextSize(1);
	gfx_setCursor(5, 260);
	gfx_puts("Iniciando captura de datos...");

	lcd_show_frame();
	msleep(8000);
	
	/******************************************************************/
	/******************************************************************/
	/******************************************************************/

	while ( 1 )
	{
		seismograph();
	}

	return 0;
}

static void seismograph(void)
{

		// lectura de button para alternar comunicacion serial
		if (gpio_get(GPIOA, GPIO0)) {
			for (volatile int i = 0; i < 3000000; i++) __asm__("nop");	
			EN_comms = !EN_comms;
		}

		for (volatile int i = 0; i < 3000000; i++) __asm__("nop");
		
		uint8_t t, I_AM;
        int16_t X, Y, Z, V;
		char X_axis[255];
		char Y_axis[255];
		char Z_axis[255];
		char V_axis[255];

		gpio_clear(GPIOC, GPIO1);             
		spi_send(SPI5, GYR_WHO_AM_I | GYR_RNW);
		spi_read(SPI5); 
		spi_send(SPI5, 0);    
		I_AM=spi_read(SPI5);
		gpio_set(GPIOC, GPIO1);

		gpio_clear(GPIOC, GPIO1);
		spi_send(SPI5, GYR_STATUS_REG | GYR_RNW);
		spi_read(SPI5);
		spi_send(SPI5, 0);
		t=spi_read(SPI5);
		gpio_set(GPIOC, GPIO1);

		gpio_clear(GPIOC, GPIO1);
		spi_send(SPI5, GYR_OUT_TEMP | GYR_RNW);
		spi_read(SPI5);
		spi_send(SPI5, 0);
		t=spi_read(SPI5);
		gpio_set(GPIOC, GPIO1);  

		gpio_clear(GPIOC, GPIO1);
		spi_send(SPI5, GYR_OUT_X_L | GYR_RNW);
		spi_read(SPI5);
		spi_send(SPI5, 0);
		X=spi_read(SPI5);
		gpio_set(GPIOC, GPIO1);

		gpio_clear(GPIOC, GPIO1);
		spi_send(SPI5, GYR_OUT_X_H | GYR_RNW);
		spi_read(SPI5);
		spi_send(SPI5, 0);
		X|=spi_read(SPI5) << 8;
		gpio_set(GPIOC, GPIO1);

		gpio_clear(GPIOC, GPIO1);
		spi_send(SPI5, GYR_OUT_Y_L | GYR_RNW);
		spi_read(SPI5);
		spi_send(SPI5, 0);
		Y=spi_read(SPI5);
		gpio_set(GPIOC, GPIO1);

		gpio_clear(GPIOC, GPIO1);
		spi_send(SPI5, GYR_OUT_Y_H | GYR_RNW);
		spi_read(SPI5);
		spi_send(SPI5, 0);
		Y|=spi_read(SPI5) << 8;
		gpio_set(GPIOC, GPIO1);

		gpio_clear(GPIOC, GPIO1);
		spi_send(SPI5, GYR_OUT_Z_L | GYR_RNW);
		spi_read(SPI5);
		spi_send(SPI5, 0);
		Z=spi_read(SPI5);
		gpio_set(GPIOC, GPIO1);

		gpio_clear(GPIOC, GPIO1);
		spi_send(SPI5, GYR_OUT_Z_H | GYR_RNW);
		spi_read(SPI5);
		spi_send(SPI5, 0);
		Z|=spi_read(SPI5) << 8;
		gpio_set(GPIOC, GPIO1);

		// escalar valores
        X = X*L3GD20_SENSITIVITY_500DPS;
        Y = Y*L3GD20_SENSITIVITY_500DPS;
        Z = Z*L3GD20_SENSITIVITY_500DPS;

		// lectura de batería
		V = read_adc_naiive(2)*0.7407407;
		uint16_t input_adc1 = read_adc_naiive(6);

		/* low Battery */
		// verificar batería baja 
		if (V < 78) {
			warning_BAT = !warning_BAT; 
			gpio_toggle(GPIOG, GPIO14);
		}
		
		// formatear a string
		sprintf(X_axis, "%d", X);
		sprintf(Y_axis, "%d", Y);
		sprintf(Z_axis, "%d", Z);
		sprintf(V_axis, "%d", V);

		// enviar datos por USART si = ON
		if (EN_comms) {
			gpio_toggle(GPIOG, GPIO13);
			console_puts(X_axis); console_puts("\t");
			console_puts(Y_axis); console_puts("\t");
			console_puts(Z_axis); console_puts("\t");
			console_puts(V_axis); console_puts("\t");
			console_puts(EN_comms ? "ON" : "OFF"); console_puts("\n");
		}
		else gpio_clear(GPIOG, GPIO13);

		// pantalla LCD
		gfx_fillScreen(LCD_BLACK);
		gfx_setTextColor(LCD_WHITE, LCD_BLACK);
		gfx_setCursor(50, 50);
		gfx_setTextSize(4);
		gfx_puts("LOGS");
		
		gfx_setTextColor(LCD_RED, LCD_BLACK);
		gfx_setCursor(15, 125);
		gfx_setTextSize(2);
		gfx_puts("X-Axis: ");
		gfx_setCursor(150, 125); 
		gfx_puts(X_axis);
		
		gfx_setTextColor(LCD_BLUE, LCD_BLACK);
		gfx_setCursor(15, 155);
		gfx_puts("Y-Axis: ");
		gfx_setCursor(150, 155);
		gfx_puts(Y_axis);
		
		gfx_setTextColor(LCD_GREEN, LCD_BLACK);
		gfx_setCursor(15, 180);
		gfx_puts("Z-Axis: ");
		gfx_setCursor(150, 180);
		gfx_puts(Z_axis);
		
		gfx_setTextColor(LCD_YELLOW, LCD_BLACK);
		gfx_setCursor(10, 225);
		gfx_setTextSize(1);
		if (EN_comms) gfx_puts("SERIAL COMMS/USB: ON");
		else gfx_puts("SERIAL COMMS/USB: OFF");
		
		gfx_setTextColor(LCD_CYAN, LCD_BLACK);
		gfx_setCursor(10, 250);
		gfx_setTextSize(1);
		gfx_puts("Battery Level: ");
		gfx_setCursor(150, 250);
		gfx_puts(V_axis);
		
		lcd_show_frame();
}

