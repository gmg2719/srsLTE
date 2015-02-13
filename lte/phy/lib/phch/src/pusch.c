/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The libLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the libLTE library.
 *
 * libLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include "prb.h"
#include "liblte/phy/phch/pusch.h"
#include "liblte/phy/phch/uci.h"
#include "liblte/phy/common/phy_common.h"
#include "liblte/phy/utils/bit.h"
#include "liblte/phy/utils/debug.h"
#include "liblte/phy/utils/vector.h"
#include "liblte/phy/filter/dft_precoding.h"

#define MAX_PUSCH_RE(cp) (2 * CP_NSYMB(cp) * 12)



const static lte_mod_t modulations[4] =
    { LTE_BPSK, LTE_QPSK, LTE_QAM16, LTE_QAM64 };
    
//#define DEBUG_IDX

#ifdef DEBUG_IDX    
cf_t *offset_original=NULL;
extern int indices[100000];
extern int indices_ptr; 
#endif


int pusch_cp(pusch_t *q, cf_t *input, cf_t *output, ra_prb_t *prb_alloc,
    uint32_t nsubframe, bool put) 
{
  return -1; 
}

/**
 * Puts PUSCH in slot number 1
 *
 * Returns the number of symbols written to sf_symbols
 *
 * 36.211 10.3 section 6.3.5
 */
int pusch_put(pusch_t *q, cf_t *pusch_symbols, cf_t *sf_symbols,
    ra_prb_t *prb_alloc, uint32_t subframe) {
  return pusch_cp(q, pusch_symbols, sf_symbols, prb_alloc, subframe, true);
}

/**
 * Extracts PUSCH from slot number 1
 *
 * Returns the number of symbols written to PUSCH
 *
 * 36.211 10.3 section 6.3.5
 */
int pusch_get(pusch_t *q, cf_t *sf_symbols, cf_t *pusch_symbols,
    ra_prb_t *prb_alloc, uint32_t subframe) {
  return pusch_cp(q, sf_symbols, pusch_symbols, prb_alloc, subframe, false);
}

/** Initializes the PDCCH transmitter and receiver */
int pusch_init(pusch_t *q, lte_cell_t cell) {
  int ret = LIBLTE_ERROR_INVALID_INPUTS;
  int i;

 if (q                         != NULL                  &&
     lte_cell_isvalid(&cell)) 
  {   
    
    bzero(q, sizeof(pusch_t));
    ret = LIBLTE_ERROR;
    
    q->cell = cell;
    q->max_symbols = q->cell.nof_prb * MAX_PUSCH_RE(q->cell.cp);

    INFO("Init PUSCH: %d ports %d PRBs, max_symbols: %d\n", q->cell.nof_ports,
        q->cell.nof_prb, q->max_symbols);

    for (i = 0; i < 4; i++) {
      if (modem_table_lte(&q->mod[i], modulations[i], true)) {
        goto clean;
      }
    }

    demod_soft_init(&q->demod, q->max_symbols);
    demod_soft_alg_set(&q->demod, APPROX);
    
    sch_init(&q->dl_sch);
    
    dft_precoding_init(&q->dft_precoding, cell.nof_prb);
    
    /* This is for equalization at receiver */
    if (precoding_init(&q->equalizer, SF_LEN_RE(cell.nof_prb, cell.cp))) {
      fprintf(stderr, "Error initializing precoding\n");
      goto clean; 
    }

    q->rnti_is_set = false; 

    // Allocate floats for reception (LLRs). Buffer casted to uint8_t for transmission
    q->pusch_q = vec_malloc(sizeof(float) * q->max_symbols * lte_mod_bits_x_symbol(LTE_QAM64));
    if (!q->pusch_q) {
      goto clean;
    }

    // Allocate floats for reception (LLRs). Buffer casted to uint8_t for transmission
    q->pusch_g = vec_malloc(sizeof(float) * q->max_symbols * lte_mod_bits_x_symbol(LTE_QAM64));
    if (!q->pusch_g) {
      goto clean;
    }
    q->pusch_d = vec_malloc(sizeof(cf_t) * q->max_symbols);
    if (!q->pusch_d) {
      goto clean;
    }

    q->ce = vec_malloc(sizeof(cf_t) * q->max_symbols);
    if (!q->ce) {
      goto clean;
    }
    q->pusch_z = vec_malloc(sizeof(cf_t) * q->max_symbols);
    if (!q->pusch_z) {
      goto clean;
    }

    ret = LIBLTE_SUCCESS;
  }
  clean: 
  if (ret == LIBLTE_ERROR) {
    pusch_free(q);
  }
  return ret;
}

void pusch_free(pusch_t *q) {
  int i;

  if (q->pusch_q) {
    free(q->pusch_q);
  }
  if (q->pusch_d) {
    free(q->pusch_d);
  }
  if (q->pusch_g) {
    free(q->pusch_g);
  }
  if (q->ce) {
    free(q->ce);
  }
  if (q->pusch_z) {
    free(q->pusch_z);
  }
  
  dft_precoding_free(&q->dft_precoding);

  precoding_free(&q->equalizer);
  
  for (i = 0; i < NSUBFRAMES_X_FRAME; i++) {
    sequence_free(&q->seq_pusch[i]);
  }

  for (i = 0; i < 4; i++) {
    modem_table_free(&q->mod[i]);
  }
  demod_soft_free(&q->demod);
  sch_free(&q->dl_sch);

  bzero(q, sizeof(pusch_t));

}

int pusch_set_rnti(pusch_t *q, uint16_t rnti) {
  uint32_t i;

  for (i = 0; i < NSUBFRAMES_X_FRAME; i++) {
    if (sequence_pusch(&q->seq_pusch[i], rnti, 2 * i, q->cell.id,
        q->max_symbols * lte_mod_bits_x_symbol(LTE_QAM64))) {
      return LIBLTE_ERROR; 
    }
  }
  q->rnti_is_set = true; 
  q->rnti = rnti; 
  return LIBLTE_SUCCESS;
}


/** Decodes the PUSCH from the received symbols
 */
int pusch_decode(pusch_t *q, cf_t *sf_symbols, cf_t *ce, float noise_estimate, uint8_t *data, uint32_t subframe, 
                 harq_t *harq_process, uint32_t rv_idx) 
{

  /* Set pointers for layermapping & precoding */
  uint32_t n;
  uint32_t nof_symbols, nof_bits, nof_bits_e;
  
  if (q                     != NULL &&
      sf_symbols            != NULL &&
      data                  != NULL &&
      subframe              <  10   &&
      harq_process          != NULL)
  {
    
    if (q->rnti_is_set) {
      nof_bits = harq_process->mcs.tbs;
      nof_symbols = 2*harq_process->prb_alloc.slot[0].nof_prb*RE_X_RB*(CP_NSYMB(q->cell.cp)-1);
      nof_bits_e = nof_symbols * lte_mod_bits_x_symbol(harq_process->mcs.mod);

      INFO("Decoding PUSCH SF: %d, Mod %s, NofBits: %d, NofSymbols: %d, NofBitsE: %d, rv_idx: %d\n",
          subframe, lte_mod_string(harq_process->mcs.mod), nof_bits, nof_symbols, nof_bits_e, rv_idx);

      /* extract symbols */
      n = pusch_get(q, sf_symbols, q->pusch_d, &harq_process->prb_alloc, subframe);
      if (n != nof_symbols) {
        fprintf(stderr, "Error expecting %d symbols but got %d\n", nof_symbols, n);
        return LIBLTE_ERROR;
      }
      
      /* extract channel estimates */
      n = pusch_get(q, ce, q->ce, &harq_process->prb_alloc, subframe);
      if (n != nof_symbols) {
        fprintf(stderr, "Error expecting %d symbols but got %d\n", nof_symbols, n);
        return LIBLTE_ERROR;
      }
      
      predecoding_single(&q->equalizer, q->pusch_d, q->ce, q->pusch_z,
            nof_symbols, noise_estimate);

      dft_predecoding(&q->dft_precoding, q->pusch_z, q->pusch_d, 
                      harq_process->prb_alloc.slot[0].nof_prb, harq_process->N_symb_ul);
      
      /* demodulate symbols 
      * The MAX-log-MAP algorithm used in turbo decoding is unsensitive to SNR estimation, 
      * thus we don't need tot set it in the LLRs normalization
      */
      demod_soft_sigma_set(&q->demod, sqrt(0.5));
      demod_soft_table_set(&q->demod, &q->mod[harq_process->mcs.mod]);
      demod_soft_demodulate(&q->demod, q->pusch_d, q->pusch_q, nof_symbols);

      /* descramble */
      scrambling_f_offset(&q->seq_pusch[subframe], q->pusch_q, 0, nof_bits_e);

      return ulsch_decode(&q->dl_sch, q->pusch_q, data, nof_bits, nof_bits_e, harq_process, rv_idx);      
    } else {
      fprintf(stderr, "Must call pusch_set_rnti() before calling pusch_decode()\n");
      return LIBLTE_ERROR; 
    }    
  } else {
    return LIBLTE_ERROR_INVALID_INPUTS;
  }
}

int pusch_encode(pusch_t *q, uint8_t *data, cf_t *sf_symbols, uint32_t subframe, 
                 harq_t *harq_process, uint32_t rv_idx) 
{
  uci_data_t uci_data; 
  bzero(&uci_data, sizeof(uci_data_t));
  return pusch_uci_encode(q, data, uci_data, sf_symbols, subframe, harq_process, rv_idx);
}

/** Converts the PUSCH data bits to symbols mapped to the slot ready for transmission
 */
int pusch_uci_encode(pusch_t *q, uint8_t *data, uci_data_t uci_data, 
                     cf_t *sf_symbols, uint32_t subframe, 
                     harq_t *harq_process, uint32_t rv_idx) 
{
  uint32_t nof_symbols, nof_bits_ulsch, nof_bits_e;
  int ret = LIBLTE_ERROR_INVALID_INPUTS; 
   
  if (q             != NULL &&
       data          != NULL &&
       subframe      <  10   &&
       harq_process  != NULL)
  {

    if (q->rnti_is_set) {
      nof_bits_ulsch = harq_process->mcs.tbs;
      nof_symbols = 2*harq_process->prb_alloc.slot[0].nof_prb*RE_X_RB*(CP_NSYMB(q->cell.cp)-1);
      nof_bits_e = nof_symbols * lte_mod_bits_x_symbol(harq_process->mcs.mod);

      if (harq_process->mcs.tbs == 0) {
        return LIBLTE_ERROR_INVALID_INPUTS;      
      }
      
      if (nof_bits_ulsch > nof_bits_e) {
        fprintf(stderr, "Invalid code rate %.2f\n", (float) nof_bits_ulsch / nof_bits_e);
        return LIBLTE_ERROR_INVALID_INPUTS;
      }

      if (nof_symbols > q->max_symbols) {
        fprintf(stderr,
            "Error too many RE per subframe (%d). PUSCH configured for %d RE (%d PRB)\n",
            nof_symbols, q->max_symbols, q->cell.nof_prb);
        return LIBLTE_ERROR_INVALID_INPUTS;
      }

      INFO("Encoding PUSCH SF: %d, Mod %s, NofBits: %d, NofSymbols: %d, NofBitsE: %d, rv_idx: %d\n",
          subframe, lte_mod_string(harq_process->mcs.mod), nof_bits_ulsch, nof_symbols, nof_bits_e, rv_idx);
      
      if (ulsch_uci_encode(&q->dl_sch, data, uci_data, q->pusch_g, harq_process, rv_idx, q->pusch_q)) 
      {
        fprintf(stderr, "Error encoding TB\n");
        return LIBLTE_ERROR;
      }
      
      scrambling_b_offset_pusch(&q->seq_pusch[subframe], (uint8_t*) q->pusch_q, 0, nof_bits_e);

      mod_modulate(&q->mod[harq_process->mcs.mod], (uint8_t*) q->pusch_q, q->pusch_d, nof_bits_e);
      
      dft_precoding(&q->dft_precoding, q->pusch_d, q->pusch_z, 
                    harq_process->prb_alloc.slot[0].nof_prb, harq_process->N_symb_ul);
      
      /* mapping to resource elements */      
      pusch_put(q, q->pusch_z, sf_symbols, &harq_process->prb_alloc, subframe);
      
      ret = LIBLTE_SUCCESS;
    } else {
     fprintf(stderr, "Must call pusch_set_rnti() to set the encoder/decoder RNTI\n");       
    }
  } 
  return ret; 
}
  