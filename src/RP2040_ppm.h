/*
  RP2040-ppm Generation PPM signals widely used in RC equipments
    Copyright (C) 2026  Francis Sourbier https://github.com/fanfanlatulipe26

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIO_CLOCK (1000000L) // 1MHz : 1 cycle = 1us
#define CYCLE_PER_MICRO (PIO_CLOCK/1000000L)

#define MAX_PPM_CHANNELS_TX 16   // max nbr for transmit. for fun ...channel numbered from 1 to ...
#define MAX_PPM_CHANNELS_RX 16   // max nbr for reader. for fun ...channel numbered from 1 to ...
#define DEFAULT_NBR_CHANNELS_TX 8
#define DEFAULT_NBR_CHANNELS_RX

#define TX_MINIMUM_FRAME  22500  //TX_MINIMUM_FRAME  us
#define TX_MINIMUM_SPACE 4000 //TX_MINIMUM_SPACE (end of frame) us. Minimum idle space
#define TX_PULSE_WIDTH  300 //TX_PULSE_WIDTH   start pulse (RISING or FALLING) for a channel us

#define RX_MINIMUM_SPACE 3200 // if a level is longer than this, it is the end-of-frame marker. us

typedef enum {
  PPP_STARTED,
  PPM_STOPPED,
  PPM_TO_BE_STOPPED,
  PPM_UNDEFINED
} PPM_Status_t;
typedef volatile uint32_t* ppmChannels_t;
class ppmWriter {
  public:
    ppmWriter(int polarity = RISING); // RISING , FALLING
    volatile uint32_t* begin(uint8_t txPin, uint8_t nbr_TX_Channels = DEFAULT_NBR_CHANNELS_TX);
    void start(void);
    void startTest(int incr = 1 );
    void stop(void);
    void end();
    void setChannel(uint8_t channel_number, uint32_t value_usec);
    uint32_t TX_minimum_frame = TX_MINIMUM_FRAME;   //TX_MINIMUM_FRAME 20000.0 (50hz)
    uint32_t TX_minimum_space = TX_MINIMUM_SPACE;   //TX_MINIMUM_SPACE (end of frame)
    uint32_t TX_pulse_width = TX_PULSE_WIDTH  ;  //TX_PULSE_WIDTH   start pulse (RISING or FALLING for a channel)

    static bool find_free_pio_sm_and_irq(PIO *pio_out, uint *sm_out, int *irq_out);
    static void release_ressources(PIO pio, uint sm, int pio_irq, pio_program ppm_program, uint offset );

  private:
    // Tracks which PIO IRQs (0 or 1) are already used by our code
    inline static uint8_t claimed_pio_irqs[NUM_PIOS] = {0};
    // Routing table : [index_pio (0 ou 1)][index_sm (0 à 3)]
    inline static ppmWriter* instances[2][4] = { { nullptr } };
    // Les 4 vecteurs d'interruption matériels possibles. Utilisé comme handler de l'IT.
    static void isr_pio0_irq0() {
      dispatch_irq(pio0, 0);
    }
    static void isr_pio0_irq1() {
      dispatch_irq(pio0, 1);
    }
    static void isr_pio1_irq0() {
      dispatch_irq(pio1, 0);
    }
    static void isr_pio1_irq1() {
      dispatch_irq(pio1, 1);
    }
    // Le aiguilleur central (statique): va renvoyer vers on_irq avec un bon contexte.
    static void dispatch_irq(PIO pio, uint irq_index);
    // La méthode d'instance (non statique !) qui a accès à toutes vos variables
    void on_irq();

    uint32_t _nbr_TX_Channels;
    int _polarity;
    uint8_t _txPin;
    volatile int32_t _remaining; // time for filling the end of a frame.
    uint offset; // for pio_add_program(_pio, &ppm_program);
    pio_sm_config _pio_conf;
    PPM_Status_t _ppm_status = PPM_UNDEFINED;
    PIO _pio;
    uint _sm = 0;
    int _pio_irq;
    // 16 MSB - strobe length in microsec
    // 16 LSB - gap length in microsec
    volatile uint32_t _TX_Channels_values[MAX_PPM_CHANNELS_TX + 1]; // [0]: used for end pulse, [1]: channel1 in us, [2]:channel2
    volatile uint _value_idx;
    bool _testMode = false;  // for test mode startTest(incr)
    uint32_t _cnt = 0; // for test mode startTest(incr)
    uint32_t _incr; // for test mode startTest(incr)
    bool ppm_program_init();
    // Stuff from pio asm
    /*
           .program ppm
          .side_set 1 opt
            set pindirs, 1 side 0; Set pin to output, low level
          .wrap_target
            out x, 16  ; potential stall with low level
          delay0:
            jmp x-- delay0 side 1
            out x, 16 side 0
          delay1:
            jmp x-- delay1 side 0
            .wrap
    */
#define ppm_wrap_target 1
#define ppm_wrap 4

    static inline const uint16_t ppm_program_instructions[] = {
      0xf081, //  0: set    pindirs, 1      side 0
      //     .wrap_target
      0x6030, //  1: out    x, 16
      0x1842, //  2: jmp    x--, 2          side 1
      0x7030, //  3: out    x, 16           side 0
      0x1044, //  4: jmp    x--, 4          side 0
      //     .wrap
    };

    static inline const struct pio_program ppm_program = {
      .instructions = ppm_program_instructions,
      .length = 5,
      .origin = -1,
      .pio_version = 0,  // added by hand to avoid compiler warning
    };

    static inline pio_sm_config ppm_program_get_default_config(uint offset) {
      pio_sm_config c = pio_get_default_sm_config();
      sm_config_set_wrap(&c, offset + ppm_wrap_target, offset + ppm_wrap);
      sm_config_set_sideset(&c, 2, true, false);
      return c;
    }
};

class ppmReader {
  public:
    ppmReader ();
    void end();
    volatile uint32_t* begin(uint8_t rxPin);
    void start(void);
    void stop(void);
    bool newFrame();
    int RX_minimum_space = RX_MINIMUM_SPACE;
  private:
    volatile uint32_t  _RX_Channels_values[MAX_PPM_CHANNELS_RX + 1];  // [0]: nbr of received channels; [1]: channel1, [2]: channels 2 in us
    uint8_t _rxPin;
    bool _newFrame = false;
    uint offset; // for pio_add_program(_pio, &ppm_program);

    pio_sm_config _pio_conf_rx;
    PPM_Status_t _ppm_status = PPM_UNDEFINED;
    PIO _pio;
    uint _sm = 0;
    int _pio_irq;
    volatile uint _value_idx;
    bool ppm_program_init_RX();

    // Routing table : [index_pio (0 ou 1)][index_sm (0 à 3)]
    inline static ppmReader* instances[2][4] = { { nullptr } };
    // Les 4 vecteurs d'interruption matériels possibles
    static void isr_pio0_irq0() {
      dispatch_irq_rx(pio0, 0);
    }
    static void isr_pio0_irq1() {
      dispatch_irq_rx(pio0, 1);
    }
    static void isr_pio1_irq0() {
      dispatch_irq_rx(pio1, 0);
    }
    static void isr_pio1_irq1() {
      dispatch_irq_rx(pio1, 1);
    }
    // L'aiguilleur central (statique)
    static void dispatch_irq_rx(PIO pio, uint irq_index);
    // La méthode d'instance (non statique !) qui a accès à toutes vos variables
    void on_irq();

    /*
        .program ppm_rx
      .wrap_target
        mov x, ~NULL            ; Reset 32-bit down-counter to 0xFFFFFFFF
      high_loop:
        jmp pin still_high      ; While pin is HIGH, keep decrementing X
        jmp low_loop            ; Pin went LOW, transition to counting LOW time
      still_high:
        jmp x-- high_loop       ; 2 cycles per loop

      low_loop:
        jmp pin next_edge       ; Rising edge detected! Finish this channel measurement
        jmp x-- low_loop        ; While pin is LOW, keep decrementing X (2 cycles per loop)

      next_edge:
        mov isr, ~x             ; Invert X to get total elapsed loop ticks (rising-to-rising)
        push noblock            ; Push channel duration to RX FIFO
      .wrap
    */
    // ------ //
    // ppm_rx //
    // ------ //

#define ppm_rx_wrap_target 0
#define ppm_rx_wrap 7

    static inline const uint16_t ppm_rx_program_instructions[] = {
      //     .wrap_target
      0xa02b, //  0: mov    x, !null
      0x00c3, //  1: jmp    pin, 3
      0x0004, //  2: jmp    4
      0x0041, //  3: jmp    x--, 1
      0x00c6, //  4: jmp    pin, 6
      0x0044, //  5: jmp    x--, 4
      0xa0c9, //  6: mov    isr, !x
      0x8000, //  7: push   noblock
      //     .wrap
    };


    static inline const struct pio_program ppm_rx_program = {
      .instructions = ppm_rx_program_instructions,
      .length = 8,
      .origin = -1,
      .pio_version = 0,  // added by hand to avoid compiler warning
    };

    static inline pio_sm_config ppm_rx_program_get_default_config(uint offset) {
      pio_sm_config c = pio_get_default_sm_config();
      sm_config_set_wrap(&c, offset + ppm_rx_wrap_target, offset + ppm_rx_wrap);
      return c;
    }
};
