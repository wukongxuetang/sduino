/*
  wiring.c - Partial implementation of the Wiring API for the ATmega8.
  Part of Arduino - http://www.arduino.cc/

  Copyright (c) 2005-2006 David A. Mellis

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General
  Public License along with this library; if not, write to the
  Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  Boston, MA  02111-1307  USA
*/

#include "wiring_private.h"


/* calculate the best prescaler and timer period for TIM4 for millis()
 *
 * The precaler factor could be calculated from the prescaler enum by
 * bit shifting, but we want to be independent from possible irregular
 * definitions for the chose CPU type.
 */
#if (clockCyclesPerMillisecond() < 256)
# define T4PRESCALER	TIM4_PRESCALER_1
# define T4PRESCALER_FACTOR	1
#elif (clockCyclesPerMillisecond() < 512)
# define T4PRESCALER	TIM4_PRESCALER_2
# define T4PRESCALER_FACTOR	2
#elif (clockCyclesPerMillisecond() < 1024)
# define T4PRESCALER	TIM4_PRESCALER_4
# define T4PRESCALER_FACTOR	4
#elif (clockCyclesPerMillisecond() < 2048)
# define T4PRESCALER	TIM4_PRESCALER_8
# define T4PRESCALER_FACTOR	8
#elif (clockCyclesPerMillisecond() < 4096)
# define T4PRESCALER	TIM4_PRESCALER_16
# define T4PRESCALER_FACTOR	16
#elif (clockCyclesPerMillisecond() < 8192)
# define T4PRESCALER	TIM4_PRESCALER_32
# define T4PRESCALER_FACTOR	32
#elif (clockCyclesPerMillisecond() < 16384)
# define T4PRESCALER	TIM4_PRESCALER_64
# define T4PRESCALER_FACTOR	64
#elif (clockCyclesPerMillisecond() < 32768)
# define T4PRESCALER	TIM4_PRESCALER_128
# define T4PRESCALER_FACTOR	128
#else
#error "could not calculate a valid prescaler für TIM4"
#endif
#define T4PERIOD	(clockCyclesPerMillisecond()/T4PRESCALER_FACTOR)

// the prescaler is set so that timer4 ticks every 64 clock cycles, and the
// the overflow handler is called every 250 ticks.
# define MICROSECONDS_PER_TIMER0_OVERFLOW (F_CPU/(T4PRESCALER_FACTOR*T4PERIOD))
//#define MICROSECONDS_PER_TIMER0_OVERFLOW (clockCyclesToMicroseconds(64 * 250))

// the whole number of milliseconds per timer4 overflow
#define MILLIS_INC (MICROSECONDS_PER_TIMER0_OVERFLOW / 1000)

// the fractional number of milliseconds per timer4 overflow. we shift right
// by three to fit these numbers into a byte. (for the clock speeds we care
// about - 8 and 16 MHz - this doesn't lose precision.)
#define FRACT_INC ((MICROSECONDS_PER_TIMER0_OVERFLOW % 1000) >> 3)
#define FRACT_MAX (1000 >> 3)

volatile unsigned long timer4_overflow_count = 0;
volatile unsigned long timer4_millis = 0;
static unsigned char timer4_fract = 0;

//void TIM4_UPD_OVF_IRQHandler(void) __interrupt(ITC_IRQ_TIM4_OVF) /* TIM4 UPD/OVF */
//void TIM4_UPD_OVF_IRQHandler(void) __interrupt(5)//FIXME
INTERRUPT_HANDLER(TIM4_UPD_OVF_IRQHandler, ITC_IRQ_TIM4_OVF) /* TIM4 UPD/OVF */
{
	// copy these to local variables so they can be stored in registers
	// (volatile variables must be read from memory on every access)
	unsigned long m = timer4_millis;
#if (FRACT_INC != 0)
	unsigned char f = timer4_fract;
#endif

	m += MILLIS_INC;
#if (FRACT_INC != 0)
	f += FRACT_INC;
	if (f >= FRACT_MAX) {
		f -= FRACT_MAX;
		m += 1;
	}

	timer4_fract  = f;
#endif
	timer4_millis = m;
	timer4_overflow_count++;

	/* Clear Interrupt Pending bit */
	TIM4_ClearITPendingBit(TIM4_IT_UPDATE);	// TIM4->SR1 = (uint8_t)(~0x01);
}

unsigned long millis()
{
	unsigned long m;

	// disable interrupts while we read timer4_millis or we might get an
	// inconsistent value (e.g. in the middle of a write to timer4_millis)
	BEGIN_CRITICAL
	m = timer4_millis;
	END_CRITICAL

	return m;
}


unsigned long micros()
{
	unsigned long m;
	uint8_t t;

	BEGIN_CRITICAL
	m = timer4_overflow_count;
	t = TIM4->CNTR;		// spl: TIM4_GetCounter()

	// check if a fresh update event is still pending
	// if (TIM4->SR1 & 0x01)
	if ((TIM4_GetFlagStatus(TIM4_IT_UPDATE)==SET) && (t < (T4PERIOD-1)))
		m++;
	END_CRITICAL

//	return ((m << 8) + t) * (64 / clockCyclesPerMicrosecond());
//	return ((m*250+t) * (64/16) );	// FIXME: use calculated value
	m *= T4PERIOD;
	m += t;
//	m <<= 2;
	m *= ((T4PRESCALER_FACTOR*1000000L)/(F_CPU));
	return m;
}


/**
 * Delay for the given number of milliseconds.
 *
 * Do a busy wait. Using the lower 16 bits of micros() is enough, as the
 * difference between the start time and the current time will never be much
 * higher than 1000.
 *
 * Using unsigned values is helpful here. This way value wrap-arounds between
 * start and now do not result in negative values but in the wanted absolute
 * difference. The magic of modulo-arithmethics.
 */
void delay(unsigned long ms)
{
	uint16_t start, now;

	start = (uint16_t) micros();	// use the lower 16 bits

	while (ms > 0) {
		yield();
		now = (uint16_t) micros();	// use the lower 16 bits
		while ( (ms > 0) && ((uint16_t)(now-start) >= 1000) ) {
			ms--;
			start += 1000;
		}
	}
}


/**
 *  Delay for the given number of microseconds.
 *
 *  Assumes a 1, 8, 12, 16, 20 or 24 MHz clock.
 */
void delayMicroseconds(unsigned int us)
{
	// call = 4 cycles, return = 4 cycles, arg access = ? cycles
	while (us--);
/*FIXME: Zeitdauer nicht ausgezählt. Das kommt raus:

                                    283 ;	wiring.c: 124: while (us--);// __asm__ ("nop");
      00818B 16 03            [ 2]  284 	ldw	y, (0x03, sp)
      00818D                        285 00101$:
      00818D 93               [ 1]  286 	ldw	x, y
      00818E 90 5A            [ 2]  287 	decw	y
      008190 5D               [ 2]  288 	tnzw	x
      008191 26 FA            [ 1]  289 	jrne	00101$
      008193 81               [ 4]  290 	ret

	Aufruf und Test: 10 + 6 für 0. Durchlauf = 16
	Pro Durchlauf: 6
*/
#if 0
//FIXME
	// call = 4 cycles + 2 to 4 cycles to init us(2 for constant delay, 4 for variable)

	// calling avrlib's delay_us() function with low values (e.g. 1 or
	// 2 microseconds) gives delays longer than desired.
	//delay_us(us);
#if F_CPU >= 24000000L
	// for the 24 MHz clock for the aventurous ones, trying to overclock

	// zero delay fix
	if (!us) return; //  = 3 cycles, (4 when true)

	// the following loop takes a 1/6 of a microsecond (4 cycles)
	// per iteration, so execute it six times for each microsecond of
	// delay requested.
	us *= 6; // x6 us, = 7 cycles

	// account for the time taken in the preceeding commands.
	// we just burned 22 (24) cycles above, remove 5, (5*4=20)
	// us is at least 6 so we can substract 5
	us -= 5; //=2 cycles

#elif F_CPU >= 20000000L
	// for the 20 MHz clock on rare Arduino boards

	// for a one-microsecond delay, simply return.  the overhead
	// of the function call takes 18 (20) cycles, which is 1us
	__asm__ __volatile__ (
		"nop" "\n\t"
		"nop" "\n\t"
		"nop" "\n\t"
		"nop"); //just waiting 4 cycles
	if (us <= 1) return; //  = 3 cycles, (4 when true)

	// the following loop takes a 1/5 of a microsecond (4 cycles)
	// per iteration, so execute it five times for each microsecond of
	// delay requested.
	us = (us << 2) + us; // x5 us, = 7 cycles

	// account for the time taken in the preceeding commands.
	// we just burned 26 (28) cycles above, remove 7, (7*4=28)
	// us is at least 10 so we can substract 7
	us -= 7; // 2 cycles

#elif F_CPU >= 16000000L
	// for the 16 MHz clock on most Arduino boards

	// for a one-microsecond delay, simply return.  the overhead
	// of the function call takes 14 (16) cycles, which is 1us
	if (us <= 1) return; //  = 3 cycles, (4 when true)

	// the following loop takes 1/4 of a microsecond (4 cycles)
	// per iteration, so execute it four times for each microsecond of
	// delay requested.
	us <<= 2; // x4 us, = 4 cycles

	// account for the time taken in the preceeding commands.
	// we just burned 19 (21) cycles above, remove 5, (5*4=20)
	// us is at least 8 so we can substract 5
	us -= 5; // = 2 cycles,

#elif F_CPU >= 12000000L
	// for the 12 MHz clock if somebody is working with USB

	// for a 1 microsecond delay, simply return.  the overhead
	// of the function call takes 14 (16) cycles, which is 1.5us
	if (us <= 1) return; //  = 3 cycles, (4 when true)

	// the following loop takes 1/3 of a microsecond (4 cycles)
	// per iteration, so execute it three times for each microsecond of
	// delay requested.
	us = (us << 1) + us; // x3 us, = 5 cycles

	// account for the time taken in the preceeding commands.
	// we just burned 20 (22) cycles above, remove 5, (5*4=20)
	// us is at least 6 so we can substract 5
	us -= 5; //2 cycles

#elif F_CPU >= 8000000L
	// for the 8 MHz internal clock

	// for a 1 and 2 microsecond delay, simply return.  the overhead
	// of the function call takes 14 (16) cycles, which is 2us
	if (us <= 2) return; //  = 3 cycles, (4 when true)

	// the following loop takes 1/2 of a microsecond (4 cycles)
	// per iteration, so execute it twice for each microsecond of
	// delay requested.
	us <<= 1; //x2 us, = 2 cycles

	// account for the time taken in the preceeding commands.
	// we just burned 17 (19) cycles above, remove 4, (4*4=16)
	// us is at least 6 so we can substract 4
	us -= 4; // = 2 cycles

#else
	// for the 1 MHz internal clock (default settings for common Atmega microcontrollers)

	// the overhead of the function calls is 14 (16) cycles
	if (us <= 16) return; //= 3 cycles, (4 when true)
	if (us <= 25) return; //= 3 cycles, (4 when true), (must be at least 25 if we want to substract 22)

	// compensate for the time taken by the preceeding and next commands (about 22 cycles)
	us -= 22; // = 2 cycles
	// the following loop takes 4 microseconds (4 cycles)
	// per iteration, so execute it us/4 times
	// us is at least 4, divided by 4 gives us 1 (no zero delay bug)
	us >>= 2; // us div 4, = 4 cycles
	

#endif

	// busy wait
	__asm__ __volatile__ (
		"1: sbiw %0,1" "\n\t" // 2 cycles
		"brne 1b" : "=w" (us) : "0" (us) // 2 cycles
	);
	// return = 4 cycles
#endif
}

#ifdef SUPPORT_ALTERNATE_MAPPINGS
/**
 * Helper function for STM8: Switch to the alternate pin functions
 *
 * a) flexible function: supports switching diffent AFR bits for
 * multiple set of pins. Not needed for stm8s103, but might be useful
 * for bigger parts.
 */
/*
void alternateFunction(uint8_t pin, uint8_t val)
{
	uint16_t optionbyte;
	uint8_t	afr;

	afr = digitalPinToAFR(pin);
	if (afr>7) return;	// ignore request on invalid pin

	optionbyte = FLASH_ReadOptionByte(OPT->OPT2);

	// flip the bit if the current state differs from requested state
	if ( ((optionbyte & (1<<afr) == 0) ^ (val==0) )
		FLASH_ProgramOptionByte(OPT->OPT2, (optionbyte&0xff)^(1<<afr));
	}
}
*/

/**
 * Helper function for STM8: Switch to the alternate pin functions
 *
 * b) simplified function: supports only bit AFR0 to switch pins C5, C6 and C7
 * from GPIO/SPI (default function) to PWM support (alternate function).
 */
void alternateFunction(uint8_t val)
{
	uint16_t optionbyte;

	optionbyte = FLASH_ReadOptionByte(0x4803) >> 8;
//	optionbyte = FLASH_ReadOptionByte(OPT->OPT2) >> 8;

	// flip the bit if the current state differs from requested state
	if ( (optionbyte & (1<<0) == 0) ^ (val==0) ) {
		FLASH_Unlock(FLASH_MEMTYPE_DATA);
//		FLASH_ProgramOptionByte(OPT->OPT2, (optionbyte&0xff)^(1<<0));
		FLASH_ProgramOptionByte(0x4803, optionbyte^(1<<0));
		FLASH_Lock(FLASH_MEMTYPE_DATA);
	}
}
#endif

void init()
{
	// free the SWIM pin to be used as a general I/O-Pin
//FIXME: CFG_GCR setzen

	// set the clock to 16 MHz
	CLK_HSIPrescalerConfig(CLK_PRESCALER_HSIDIV1);
	
	GPIO_DeInit(GPIOA);
	GPIO_DeInit(GPIOB);
	GPIO_DeInit(GPIOC);
	GPIO_DeInit(GPIOD);
	GPIO_DeInit(GPIOE);

	UART1_DeInit();

	// set timer 0 prescale factor to 64, period 0 (=256)
	TIM4_DeInit();
//	TIM4_TimeBaseInit(TIM4_PRESCALER_64, 249);
	TIM4_TimeBaseInit(T4PRESCALER, (uint8_t) T4PERIOD-1);
	/* Clear TIM4 update flag */
	TIM4_ClearFlag(TIM4_FLAG_UPDATE);	// TIM4->SR1 = (uint8_t)(~0x01);
	/* Enable update interrupt */
	TIM4_ITConfig(TIM4_IT_UPDATE, ENABLE);	// TIM4->IER |= (uint8_t)TIM4_IT;
	/* Enable TIM4 */
	TIM4_Cmd(ENABLE);	// TIM4->CR1 |= TIM4_CR1_CEN;
	

	// timers 1 and 2 are used for phase-correct hardware pwm
	// this is better for motors as it ensures an even waveform
	// note, however, that fast pwm mode can achieve a frequency of up
	// 8 MHz (with a 16 MHz clock) at 50% duty cycle

	TIM1_DeInit();
	TIM1_TimeBaseInit(64, TIM1_COUNTERMODE_UP, 255, 0);
	TIM1_Cmd(ENABLE);

	TIM1_OC1Init(
		TIM1_OCMODE_PWM2,
		TIM1_OUTPUTSTATE_DISABLE,
		TIM1_OUTPUTNSTATE_DISABLE,
		0,
		TIM1_OCPOLARITY_HIGH,
		TIM1_OCNPOLARITY_HIGH,
		TIM1_OCIDLESTATE_SET,
		TIM1_OCNIDLESTATE_SET
	);
	TIM1_OC2Init(
		TIM1_OCMODE_PWM2,
		TIM1_OUTPUTSTATE_DISABLE,
		TIM1_OUTPUTNSTATE_DISABLE,
		0,
		TIM1_OCPOLARITY_HIGH,
		TIM1_OCNPOLARITY_HIGH,
		TIM1_OCIDLESTATE_SET,
		TIM1_OCNIDLESTATE_SET
	);
	TIM1_OC3Init(
		TIM1_OCMODE_PWM2,
		TIM1_OUTPUTSTATE_DISABLE,
		TIM1_OUTPUTNSTATE_DISABLE,
		0,
		TIM1_OCPOLARITY_HIGH,
		TIM1_OCNPOLARITY_HIGH,
		TIM1_OCIDLESTATE_SET,
		TIM1_OCNIDLESTATE_SET
	);
	TIM1_OC4Init(
		TIM1_OCMODE_PWM2,
		TIM1_OUTPUTSTATE_DISABLE,
		0,
		TIM1_OCPOLARITY_HIGH,
		TIM1_OCIDLESTATE_SET
	);
	TIM1_Cmd(ENABLE);
	TIM1_CtrlPWMOutputs(ENABLE);

	TIM2_DeInit();
	TIM2_TimeBaseInit(TIM2_PRESCALER_64, 255);

	TIM2_OC1Init(
		TIM2_OCMODE_PWM1,
		TIM2_OUTPUTSTATE_DISABLE,
		0,
		TIM2_OCPOLARITY_HIGH
	);

	TIM2_OC2Init(
		TIM2_OCMODE_PWM1,
		TIM2_OUTPUTSTATE_DISABLE,
		0,
		TIM2_OCPOLARITY_HIGH
	);

	TIM2_OC3Init(
		TIM2_OCMODE_PWM1,
		TIM2_OUTPUTSTATE_DISABLE,
		0,
		TIM2_OCPOLARITY_HIGH
	);
	TIM2_OC1PreloadConfig(ENABLE); // TIM2->CCMR1 |= (uint8_t)TIM2_CCMR_OCxPE;
	TIM2_OC2PreloadConfig(ENABLE); // TIM2->CCMR2 |= (uint8_t)TIM2_CCMR_OCxPE;
	TIM2_OC3PreloadConfig(ENABLE); // TIM2->CCMR3 |= (uint8_t)TIM2_CCMR_OCxPE;
	TIM2_Cmd(ENABLE);	// TIM2->CR1 |= (uint8_t)TIM2_CR1_CEN;

	/* De-Init ADC peripheral, sets prescaler to 2 */
	ADC1_DeInit();

	// set a2d prescaler so we are inside a range of 1-2 MHz
	#if F_CPU >= 18000000 // 18 MHz / 18 = 1000 KHz
		ADC1->CR1 = 7 <<4;
	#elif F_CPU >= 12000000 // 12 MHz / 12 = 1000 kHz
		ADC1->CR1 = 6 <<4;
	#elif F_CPU >= 10000000 // 10 MHz / 10 = 1000 kHz
		ADC1->CR1 = 5 <<4;
	#elif F_CPU >= 8000000 // 8 MHz / 8 = 1000 kHz
		ADC1->CR1 = 4 <<4;
	#elif F_CPU >= 6000000 // 6 MHz / 6 = 1000 kHz
		ADC1->CR1 = 4 <<4;
	#elif F_CPU >= 4000000 // 4 MHz / 4 = 1000 kHz
		ADC1->CR1 = 3 <<4;
	#elif F_CPU >= 3000000 // 3 MHz / 3 = 1000 kHz
		ADC1->CR1 = 2 <<4;
	#elif F_CPU >= 2000000 // 2 MHz / 2 = 1000 kHz
		ADC1->CR1 = 1 <<4;
	#else // minimum prescaler is 2
		ADC1->CR1 = 0 <<4;
	#endif
#if 0
	// the bootloader connects pins 0 and 1 to the USART; disconnect them
	// here so they can be used as normal digital i/o; they will be
	// reconnected in Serial.begin()
#if defined(UCSRB)
	UCSRB = 0;
#elif defined(UCSR0B)
	UCSR0B = 0;
#endif
#endif //if 0
	// this needs to be called before setup() or some functions won't
	// work there
	enableInterrupts();
}
//void main(){init();}