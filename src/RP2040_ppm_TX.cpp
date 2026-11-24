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
#include <hardware/pio.h>
#include "RP2040_ppm.h"

ppmWriter:: ppmWriter(int polarity) { // RISING or FALLING
  _polarity = polarity;
}

// end():  release the ressources : sm , irq handler,
void ppmWriter::end() {
  release_ressources(_pio, _sm, _pio_irq, ppm_program, offset );
}

volatile uint32_t* ppmWriter::begin(uint8_t txPin, uint8_t nbr_TX_Channels)
{
  _nbr_TX_Channels = nbr_TX_Channels;
  _txPin = txPin;

  _TX_Channels_values[0] = TX_minimum_space + TX_pulse_width;
  // Neutral position (1500us) by default
  for (uint32_t i = 1; i <= _nbr_TX_Channels; ++i)
    _TX_Channels_values[i] = 1500;
  bool result = ppm_program_init();

  if (!result) return NULL;
  _ppm_status = PPM_STOPPED;
  return _TX_Channels_values;
};


void ppmWriter::start(void)
{
  _value_idx = 1;
  _remaining = TX_minimum_frame;

  // pio_set_irq0_source_enabled(_pio, pis_sm0_tx_fifo_not_full, true);
  // 1. Get the interrupt source position for the specific state machine
  pio_interrupt_source_t source = pio_get_tx_fifo_not_full_interrupt_source(_sm);
  uint irq_index = (_pio_irq - PIO0_IRQ_0) & 1;  // give 0 for PIO0_IRQ_0/PIO1_IRQ_0 and 1 for PIO0_IRQ_1/PIO1_IRQ_1
  pio_set_irqn_source_enabled (_pio, irq_index, source, true);
  pio_sm_clear_fifos(_pio, _sm); // FS
  // setup the IRQ handler
  if (_pio == pio0) {
    if (_pio_irq == PIO0_IRQ_0) {
      irq_set_exclusive_handler(PIO0_IRQ_0, isr_pio0_irq0);
      irq_set_enabled(PIO0_IRQ_0, true);
    } else {
      irq_set_exclusive_handler(PIO0_IRQ_1, isr_pio0_irq1);
      irq_set_enabled(PIO0_IRQ_1, true);
    }
  }
  else {  // pio1
    if (_pio_irq == PIO1_IRQ_0) {
      irq_set_exclusive_handler(PIO1_IRQ_0, isr_pio1_irq0);
      irq_set_enabled(PIO1_IRQ_0, true);
    } else {
      irq_set_exclusive_handler(PIO1_IRQ_1, isr_pio1_irq1);
      irq_set_enabled(PIO1_IRQ_1, true);
    }
  }
  pio_sm_set_enabled(_pio, _sm, true);  // let's go
  _ppm_status = PPP_STARTED;
}

void ppmWriter::startTest(int incr) {
  _testMode = true;
  _cnt = 0;
  _incr = incr;
  start();
}


void ppmWriter::setChannel(uint8_t channel_number, uint32_t value_usec) {
  _TX_Channels_values[channel_number] = value_usec;
}

void ppmWriter::stop(void) {
  // Stop nicely at the end of a frame
  // we will stop feeding the FIFO when we send le last channel of  frame (index 0)
  _ppm_status = PPM_TO_BE_STOPPED;  // will be handled in the ISR

  /*
    Disables the state machine (if running)
    Clears the FIFOs
    Applies the configuration specified by 'config'
    Resets any internal state e.g. shift counters
    Jumps to the initial program location given by 'initial_pc'
  */

  // wait until state machine is stalled: it will be the end of a frame.
  uint32_t SM_STALL_MASK = 1u << (PIO_FDEBUG_TXSTALL_LSB + _sm);
  // Clear the sticky stall status.
  _pio->fdebug = SM_STALL_MASK;
  // Wait until the stall flag is up again.
  while (!(_pio->fdebug & SM_STALL_MASK));
  pio_sm_init (_pio, _sm, offset, &_pio_conf);
  pio_sm_clear_fifos(_pio, _sm);
  pio_sm_set_clkdiv(_pio, _sm, (float) clock_get_hz(clk_sys) / PIO_CLOCK); //1MHz
  _ppm_status = PPM_STOPPED;
  _testMode = false;
}


void ppmWriter::dispatch_irq(PIO pio, uint irq_index) {
  uint pio_idx = pio_get_index(pio);
  uint32_t status = (irq_index == 0) ? pio->ints0 : pio->ints1;

  // Vérifier chacune des 4 State Machines du bloc PIO
  for (uint sm = 0; sm < 4; sm++) {
    // Masque combinant les 3 causes possibles pour la SM 'sm' :
    // Bit 'sm' (RXNEMPTY) | Bit 'sm + 4' (TXNFULL) | Bit 'sm + 8' (Instruction IRQ)
    uint32_t sm_mask = (1u << sm) | (1u << (sm + 4)) | (1u << (sm + 8));

    if (status & sm_mask) {
      ppmWriter* target = instances[pio_idx][sm];
      if (target != nullptr) {
        target->on_irq(); // Appel dans le contexte normal de l'objet !
      }
    }
  }
}

// 16 MSB - strobe length in microsec
// 16 LSB - gap length in microsec
#define BUILD_CH_VALUE(value_us) (((TX_pulse_width*CYCLE_PER_MICRO-1) << 16) + (value_us - TX_pulse_width)*CYCLE_PER_MICRO-3)  //
// Is called from the ISR dispacher, in the context of the object.
void ppmWriter::on_irq() {
  while (!pio_sm_is_tx_fifo_full(_pio, _sm)) {
    if ( _value_idx == 0) {  // last pulse. Sync
      if (_remaining < 0 || (uint32_t)_remaining < TX_minimum_space) {
        _remaining = (int32_t)(TX_minimum_space + TX_pulse_width);
      }
      _TX_Channels_values[0] = _remaining;
      _remaining = TX_minimum_frame;
      // if in testmode (startTest) increment the value of the channels.
      if (_testMode) {
        _cnt = (_cnt + _incr) % 1000;
        for (uint i = 1; i <= _nbr_TX_Channels; i++) _TX_Channels_values[i] = 1000 + _cnt;
      }
    }
    else
      _remaining = _remaining - _TX_Channels_values[_value_idx]; // may become negative ...
    pio_sm_put(_pio, _sm, BUILD_CH_VALUE(_TX_Channels_values[_value_idx]));
    if ( _value_idx == 0) {
      if (  _ppm_status == PPM_TO_BE_STOPPED) {
        irq_set_enabled(_pio_irq, false);
        pio_interrupt_clear(_pio, _pio_irq);
      }
    }
    _value_idx = (_value_idx + 1) % (_nbr_TX_Channels + 1);
  }
}


/**
   Finds and claims a free PIO state machine on a PIO block that also has a free IRQ (0 or 1).

   @param pio_out Pointer to store the found PIO instance (pio0, pio1, etc.)
   @param sm_out  Pointer to store the claimed state machine index (0-3)
   @param irq_out Pointer to store the system IRQ number (e.g., PIO0_IRQ_0)
   @return true if a PIO, SM, and IRQ were found and claimed; false otherwise.
*/
bool ppmWriter::find_free_pio_sm_and_irq(PIO * pio_out, uint * sm_out, int *irq_out) {
  PIO pios[NUM_PIOS] = {
    pio0,
    pio1
#if NUM_PIOS > 2
    , pio2 // Automatically supports RP2350 (Pico 2)
#endif
  };
  for (uint i = 0; i < NUM_PIOS; i++) {
    PIO current_pio = pios[i];
    // 1. Check if either IRQ 0 or IRQ 1 is free on this PIO block
    int found_irq_idx = -1;
    for (int irq_idx = 0; irq_idx < 2; irq_idx++) {
      uint sys_irq = pio_get_irq_num(current_pio, irq_idx);

      // Check both our internal tracker and the SDK's exclusive handler table
      bool is_claimed = (claimed_pio_irqs[i] & (1 << irq_idx)) != 0;
      bool has_exclusive_handler = irq_get_exclusive_handler(sys_irq) != NULL;
      if (!is_claimed && !has_exclusive_handler) {
        found_irq_idx = irq_idx;
        break;
      }
    }

    // If neither IRQ 0 nor IRQ 1 is available on this PIO, skip to the next PIO
    if (found_irq_idx == -1) {
      continue;
    }
    // 2. Try to claim an unused state machine on this PIO
    int sm = pio_claim_unused_sm(current_pio, false);
    if (sm >= 0) {
      // Mark this IRQ as claimed in our tracker
      claimed_pio_irqs[i] |= (1 << found_irq_idx);
      // Populate output parameters
      *pio_out = current_pio;
      *sm_out = (uint)sm;
      *irq_out = pio_get_irq_num(current_pio, found_irq_idx);
      return true;
    }
  }
  return false;
}

// called from begin()
bool ppmWriter::ppm_program_init() {
  // find a state machine in a pio with a free interrupt.
  bool reserved = find_free_pio_sm_and_irq(&_pio, &_sm, &_pio_irq);
  if (!reserved) {
    return false;
  }
  else {
  }
  offset = pio_add_program(_pio, &ppm_program);
  if (offset < 0) return false;
  uint pio_idx = pio_get_index(_pio); // Retourne 0 pour pio0, 1 pour pio1
  // 1. Enregistrer cette instance dans la table
  instances[pio_idx][_sm] = this;
  pio_gpio_init(_pio, _txPin);
  if ( _polarity == FALLING) {
    //  if inverted force idle high at the beginning and inver the output .
    gpio_set_outover(_txPin, GPIO_OVERRIDE_INVERT);
    pio_sm_set_pins_with_mask(_pio, _sm, 0, 1u << _txPin); // 0 inverted give 1
    pio_sm_set_consecutive_pindirs(_pio, _sm, _txPin, 1, true);
  }
  delay(20); // at the very beginning we need a long steady stop/reset level. 20ms ...
  // pio_gpio_init(_pio, _txPin);
  _pio_conf = ppm_program_get_default_config(offset);  // config as generated by pio asm.
  // Here we add our specific configuration.

  sm_config_set_sideset_pin_base (&_pio_conf, _txPin);
  sm_config_set_set_pins(&_pio_conf, _txPin, 1);
  sm_config_set_out_shift(&_pio_conf, false, true, 32);  // lest shift for OSR, autopull threshold 32
  sm_config_set_fifo_join(&_pio_conf, PIO_FIFO_JOIN_TX);  // 8 word FIFO
  pio_sm_init(_pio, _sm, offset, &_pio_conf);
  pio_sm_set_clkdiv(_pio, _sm, (float) clock_get_hz(clk_sys) / PIO_CLOCK); //1MHz
  pio_sm_clear_fifos(_pio, _sm);
  return true;
}

// helper for ppmWriter::end() and ppmReader:: end()
void ppmWriter::release_ressources(PIO pio, uint sm, int pio_irq, pio_program ppm_program, uint offset ) {
  // STop execution of the State Machine
  pio_sm_set_enabled(pio, sm, false);
  // Clear FIFO ...
  pio_sm_clear_fifos(pio, sm);
  // Reinit internal state3of the SM (registers, PC, ...)
  pio_sm_restart(pio, sm);
  // Remove the exclusive interrupt handler registered for the PIO IRQ
  irq_remove_handler(pio_irq, irq_get_exclusive_handler(pio_irq));

  // update the table where we track the pio&IRQ0/1 used, release the sm, and unload the PIO program
  uint pio_idx = pio_get_index(pio);
  // Determine if _pio_irq corresponds to IRQ 0 or IRQ 1
  if (pio_irq == pio_get_irq_num(pio, 0)) {
    claimed_pio_irqs[pio_idx] &= ~(1 << 0);
  } else if (pio_irq == pio_get_irq_num(pio, 1)) {
    claimed_pio_irqs[pio_idx] &= ~(1 << 1);
  }
  // delete pio program from pio memory and free et State Machine
  pio_remove_program_and_unclaim_sm (&ppm_program, pio, sm, offset);
}
