/*
 * jdhuff.c
 *
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * Copyright (C) 2009-2011, D. R. Commander.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains Huffman entropy decoding routines.
 *
 * Much of the complexity here has to do with supporting input suspension.
 * If the data source module demands suspension, we want to be able to back
 * up to the start of the current MCU.  To do this, we copy state variables
 * into local working storage, and update them back to the permanent
 * storage only upon successful completion of an MCU.
 */

#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jdhuff.h"		/* Declarations shared with jdphuff.c */
#include "jpegcomp.h"


/*
 * Expanded entropy decoder object for Huffman decoding.
 *
 * The savable_state subrecord contains fields that change within an MCU,
 * but must not be updated permanently until we complete the MCU.
 */

typedef struct {
  int last_dc_val[MAX_COMPS_IN_SCAN]; /* last DC coef for each component */
} savable_state;

/* This macro is to work around compilers with missing or broken
 * structure assignment.  You'll need to fix this code if you have
 * such a compiler and you change MAX_COMPS_IN_SCAN.
 */

#ifndef NO_STRUCT_ASSIGN
#define ASSIGN_STATE(dest,src)  ((dest) = (src))
#else
#if MAX_COMPS_IN_SCAN == 4
#define ASSIGN_STATE(dest,src)  \
	((dest).last_dc_val[0] = (src).last_dc_val[0], \
	 (dest).last_dc_val[1] = (src).last_dc_val[1], \
	 (dest).last_dc_val[2] = (src).last_dc_val[2], \
	 (dest).last_dc_val[3] = (src).last_dc_val[3])
#endif
#endif


typedef struct {
  struct jpeg_entropy_decoder pub; /* public fields */

  /* These fields are loaded into local variables at start of each MCU.
   * In case of suspension, we exit WITHOUT updating them.
   */
  bitread_perm_state bitstate;	/* Bit buffer at start of MCU */
  savable_state saved;		/* Other state at start of MCU */

  /* These fields are NOT loaded into local working state. */
  unsigned int restarts_to_go;	/* MCUs left in this restart interval */

  /* Pointers to derived tables (these workspaces have image lifespan) */
  d_derived_tbl * dc_derived_tbls[NUM_HUFF_TBLS];
  d_derived_tbl * ac_derived_tbls[NUM_HUFF_TBLS];

  /* Precalculated info set up by start_pass for use in decode_mcu: */

  /* Pointers to derived tables to be used for each block within an MCU */
  d_derived_tbl * dc_cur_tbls[D_MAX_BLOCKS_IN_MCU];
  d_derived_tbl * ac_cur_tbls[D_MAX_BLOCKS_IN_MCU];
  /* Whether we care about the DC and AC coefficient values for each block */
  boolean dc_needed[D_MAX_BLOCKS_IN_MCU];
  boolean ac_needed[D_MAX_BLOCKS_IN_MCU];
} huff_entropy_decoder;

typedef huff_entropy_decoder * huff_entropy_ptr;

#ifdef ANDROID

METHODDEF(boolean)
decode_mcu_discard_coef (j_decompress_ptr cinfo);

METHODDEF(void)
configure_huffman_decoder(j_decompress_ptr cinfo, huffman_offset_data offset);

METHODDEF(void)
get_huffman_decoder_configuration(j_decompress_ptr cinfo,
        huffman_offset_data *offset);
#endif

/*
 * Huffman table setup routines
 */

LOCAL(void)
add_huff_table (j_decompress_ptr cinfo,
		JHUFF_TBL **htblptr, const UINT8 *bits, const UINT8 *val)
/* Define a Huffman table */
{
  int nsymbols, len;

  if (*htblptr == NULL)
    *htblptr = jpeg_alloc_huff_table((j_common_ptr) cinfo);

  /* Copy the number-of-symbols-of-each-code-length counts */
  MEMCOPY((*htblptr)->bits, bits, SIZEOF((*htblptr)->bits));

  /* Validate the counts.  We do this here mainly so we can copy the right
   * number of symbols from the val[] array, without risking marching off
   * the end of memory.  jchuff.c will do a more thorough test later.
   */
  nsymbols = 0;
  for (len = 1; len <= 16; len++)
    nsymbols += bits[len];
  if (nsymbols < 1 || nsymbols > 256)
    ERREXIT(cinfo, JERR_BAD_HUFF_TABLE);

  MEMCOPY((*htblptr)->huffval, val, nsymbols * SIZEOF(UINT8));

  /* Initialize sent_table FALSE so table will be written to JPEG file. */
  (*htblptr)->sent_table = FALSE;
}


LOCAL(void)
std_huff_tables (j_decompress_ptr cinfo)
/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */
{
  static const UINT8 bits_dc_luminance[17] =
    { /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
  static const UINT8 val_dc_luminance[] =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
  
  static const UINT8 bits_dc_chrominance[17] =
    { /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
  static const UINT8 val_dc_chrominance[] =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
  
  static const UINT8 bits_ac_luminance[17] =
    { /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
  static const UINT8 val_ac_luminance[] =
    { 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
      0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
      0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
      0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
      0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
      0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
      0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
      0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
      0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
      0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
      0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
      0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
      0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
      0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
      0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
      0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
      0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
      0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
      0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
      0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa };
  
  static const UINT8 bits_ac_chrominance[17] =
    { /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
  static const UINT8 val_ac_chrominance[] =
    { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
      0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
      0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
      0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
      0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
      0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
      0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
      0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
      0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
      0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
      0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
      0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
      0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
      0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
      0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
      0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
      0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
      0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
      0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa };
  
  add_huff_table(cinfo, &cinfo->dc_huff_tbl_ptrs[0],
		 bits_dc_luminance, val_dc_luminance);
  add_huff_table(cinfo, &cinfo->ac_huff_tbl_ptrs[0],
		 bits_ac_luminance, val_ac_luminance);
  add_huff_table(cinfo, &cinfo->dc_huff_tbl_ptrs[1],
		 bits_dc_chrominance, val_dc_chrominance);
  add_huff_table(cinfo, &cinfo->ac_huff_tbl_ptrs[1],
		 bits_ac_chrominance, val_ac_chrominance);
}

/*
 * Initialize for a Huffman-compressed scan.
 */

METHODDEF(void)
start_pass_huff_decoder (j_decompress_ptr cinfo)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;
  int ci, blkn, dctbl, actbl;
  jpeg_component_info * compptr;

  /* Check that the scan parameters Ss, Se, Ah/Al are OK for sequential JPEG.
   * This ought to be an error condition, but we make it a warning because
   * there are some baseline files out there with all zeroes in these bytes.
   */
  if (cinfo->Ss != 0 || cinfo->Se != DCTSIZE2-1 ||
      cinfo->Ah != 0 || cinfo->Al != 0)
    WARNMS(cinfo, JWRN_NOT_SEQUENTIAL);

  for (ci = 0; ci < cinfo->comps_in_scan; ci++) {
    compptr = cinfo->cur_comp_info[ci];
    dctbl = compptr->dc_tbl_no;
    actbl = compptr->ac_tbl_no;
    /* Compute derived values for Huffman tables */
    /* We may do this more than once for a table, but it's not expensive */
    jpeg_make_d_derived_tbl(cinfo, TRUE, dctbl,
			    & entropy->dc_derived_tbls[dctbl]);
    jpeg_make_d_derived_tbl(cinfo, FALSE, actbl,
			    & entropy->ac_derived_tbls[actbl]);
    /* Initialize DC predictions to 0 */
    entropy->saved.last_dc_val[ci] = 0;
  }

  /* Precalculate decoding info for each block in an MCU of this scan */
  for (blkn = 0; blkn < cinfo->blocks_in_MCU; blkn++) {
    ci = cinfo->MCU_membership[blkn];
    compptr = cinfo->cur_comp_info[ci];
    /* Precalculate which table to use for each block */
    entropy->dc_cur_tbls[blkn] = entropy->dc_derived_tbls[compptr->dc_tbl_no];
    entropy->ac_cur_tbls[blkn] = entropy->ac_derived_tbls[compptr->ac_tbl_no];
    /* Decide whether we really care about the coefficient values */
    if (compptr->component_needed) {
      entropy->dc_needed[blkn] = TRUE;
      /* we don't need the ACs if producing a 1/8th-size image */
      entropy->ac_needed[blkn] = (compptr->_DCT_scaled_size > 1);
    } else {
      entropy->dc_needed[blkn] = entropy->ac_needed[blkn] = FALSE;
    }
  }

  /* Initialize bitread state variables */
  entropy->bitstate.bits_left = 0;
  entropy->bitstate.get_buffer = 0; /* unnecessary, but keeps Purify quiet */
  entropy->pub.insufficient_data = FALSE;

  /* Initialize restart counter */
  entropy->restarts_to_go = cinfo->restart_interval;
}


/*
 * Compute the derived values for a Huffman table.
 * This routine also performs some validation checks on the table.
 *
 * Note this is also used by jdphuff.c.
 */

GLOBAL(void)
jpeg_make_d_derived_tbl (j_decompress_ptr cinfo, boolean isDC, int tblno,
			 d_derived_tbl ** pdtbl)
{
  JHUFF_TBL *htbl;
  d_derived_tbl *dtbl;
  int p, i, l, si, numsymbols;
  int lookbits, ctr;
  char huffsize[257];
  unsigned int huffcode[257];
  unsigned int code;

  /* Note that huffsize[] and huffcode[] are filled in code-length order,
   * paralleling the order of the symbols themselves in htbl->huffval[].
   */

  /* Find the input Huffman table */
  if (tblno < 0 || tblno >= NUM_HUFF_TBLS)
    ERREXIT1(cinfo, JERR_NO_HUFF_TABLE, tblno);
  if (cinfo->dc_huff_tbl_ptrs[0] == NULL && cinfo->dc_huff_tbl_ptrs[1] == NULL && cinfo->ac_huff_tbl_ptrs[0] == NULL && cinfo->ac_huff_tbl_ptrs[1] == NULL)
    std_huff_tables(cinfo);
  htbl =
    isDC ? cinfo->dc_huff_tbl_ptrs[tblno] : cinfo->ac_huff_tbl_ptrs[tblno];
  if (htbl == NULL)
    ERREXIT1(cinfo, JERR_NO_HUFF_TABLE, tblno);

  /* Allocate a workspace if we haven't already done so. */
  if (*pdtbl == NULL)
    *pdtbl = (d_derived_tbl *)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				  SIZEOF(d_derived_tbl));
  dtbl = *pdtbl;
  dtbl->pub = htbl;		/* fill in back link */
  
  /* Figure C.1: make table of Huffman code length for each symbol */

  p = 0;
  for (l = 1; l <= 16; l++) {
    i = (int) htbl->bits[l];
    if (i < 0 || p + i > 256)	/* protect against table overrun */
      ERREXIT(cinfo, JERR_BAD_HUFF_TABLE);
    while (i--)
      huffsize[p++] = (char) l;
  }
  huffsize[p] = 0;
  numsymbols = p;
  
  /* Figure C.2: generate the codes themselves */
  /* We also validate that the counts represent a legal Huffman code tree. */
  
  code = 0;
  si = huffsize[0];
  p = 0;
  while (huffsize[p]) {
    while (((int) huffsize[p]) == si) {
      huffcode[p++] = code;
      code++;
    }
    /* code is now 1 more than the last code used for codelength si; but
     * it must still fit in si bits, since no code is allowed to be all ones.
     */
    if (((INT32) code) >= (((INT32) 1) << si))
      ERREXIT(cinfo, JERR_BAD_HUFF_TABLE);
    code <<= 1;
    si++;
  }

  /* Figure F.15: generate decoding tables for bit-sequential decoding */

  p = 0;
  for (l = 1; l <= 16; l++) {
    if (htbl->bits[l]) {
      /* valoffset[l] = huffval[] index of 1st symbol of code length l,
       * minus the minimum code of length l
       */
      dtbl->valoffset[l] = (INT32) p - (INT32) huffcode[p];
      p += htbl->bits[l];
      dtbl->maxcode[l] = huffcode[p-1]; /* maximum code of length l */
    } else {
      dtbl->maxcode[l] = -1;	/* -1 if no codes of this length */
    }
  }
  dtbl->valoffset[17] = 0;
  dtbl->maxcode[17] = 0xFFFFFL; /* ensures jpeg_huff_decode terminates */

  /* Compute lookahead tables to speed up decoding.
   * First we set all the table entries to 0, indicating "too long";
   * then we iterate through the Huffman codes that are short enough and
   * fill in all the entries that correspond to bit sequences starting
   * with that code.
   */

   for (i = 0; i < (1 << HUFF_LOOKAHEAD); i++)
     dtbl->lookup[i] = (HUFF_LOOKAHEAD + 1) << HUFF_LOOKAHEAD;

  p = 0;
  for (l = 1; l <= HUFF_LOOKAHEAD; l++) {
    for (i = 1; i <= (int) htbl->bits[l]; i++, p++) {
      /* l = current code's length, p = its index in huffcode[] & huffval[]. */
      /* Generate left-justified code followed by all possible bit sequences */
      lookbits = huffcode[p] << (HUFF_LOOKAHEAD-l);
      for (ctr = 1 << (HUFF_LOOKAHEAD-l); ctr > 0; ctr--) {
	dtbl->lookup[lookbits] = (l << HUFF_LOOKAHEAD) | htbl->huffval[p];
	lookbits++;
      }
    }
  }

  /* Validate symbols as being reasonable.
   * For AC tables, we make no check, but accept all byte values 0..255.
   * For DC tables, we require the symbols to be in range 0..15.
   * (Tighter bounds could be applied depending on the data depth and mode,
   * but this is sufficient to ensure safe decoding.)
   */
  if (isDC) {
    for (i = 0; i < numsymbols; i++) {
      int sym = htbl->huffval[i];
      if (sym < 0 || sym > 15)
	ERREXIT(cinfo, JERR_BAD_HUFF_TABLE);
    }
  }
}


/*
 * Out-of-line code for bit fetching (shared with jdphuff.c).
 * See jdhuff.h for info about usage.
 * Note: current values of get_buffer and bits_left are passed as parameters,
 * but are returned in the corresponding fields of the state struct.
 *
 * On most machines MIN_GET_BITS should be 25 to allow the full 32-bit width
 * of get_buffer to be used.  (On machines with wider words, an even larger
 * buffer could be used.)  However, on some machines 32-bit shifts are
 * quite slow and take time proportional to the number of places shifted.
 * (This is true with most PC compilers, for instance.)  In this case it may
 * be a win to set MIN_GET_BITS to the minimum value of 15.  This reduces the
 * average shift distance at the cost of more calls to jpeg_fill_bit_buffer.
 */

#ifdef SLOW_SHIFT_32
#define MIN_GET_BITS  15	/* minimum allowable value */
#else
#define MIN_GET_BITS  (BIT_BUF_SIZE-7)
#endif


GLOBAL(boolean)
jpeg_fill_bit_buffer (bitread_working_state * state,
		      register bit_buf_type get_buffer, register int bits_left,
		      int nbits)
/* Load up the bit buffer to a depth of at least nbits */
{
  /* Copy heavily used state fields into locals (hopefully registers) */
  register const JOCTET * next_input_byte = state->next_input_byte;
  register size_t bytes_in_buffer = state->bytes_in_buffer;
  j_decompress_ptr cinfo = state->cinfo;

  /* Attempt to load at least MIN_GET_BITS bits into get_buffer. */
  /* (It is assumed that no request will be for more than that many bits.) */
  /* We fail to do so only if we hit a marker or are forced to suspend. */

  if (cinfo->unread_marker == 0) {	/* cannot advance past a marker */
    while (bits_left < MIN_GET_BITS) {
      register int c;

      /* Attempt to read a byte */
      if (bytes_in_buffer == 0) {
	if (! (*cinfo->src->fill_input_buffer) (cinfo))
	  return FALSE;
	next_input_byte = cinfo->src->next_input_byte;
	bytes_in_buffer = cinfo->src->bytes_in_buffer;
      }
      bytes_in_buffer--;
      c = GETJOCTET(*next_input_byte++);

      /* If it's 0xFF, check and discard stuffed zero byte */
      if (c == 0xFF) {
	/* Loop here to discard any padding FF's on terminating marker,
	 * so that we can save a valid unread_marker value.  NOTE: we will
	 * accept multiple FF's followed by a 0 as meaning a single FF data
	 * byte.  This data pattern is not valid according to the standard.
	 */
	do {
	  if (bytes_in_buffer == 0) {
	    if (! (*cinfo->src->fill_input_buffer) (cinfo))
	      return FALSE;
	    next_input_byte = cinfo->src->next_input_byte;
	    bytes_in_buffer = cinfo->src->bytes_in_buffer;
	  }
	  bytes_in_buffer--;
	  c = GETJOCTET(*next_input_byte++);
	} while (c == 0xFF);

	if (c == 0) {
	  /* Found FF/00, which represents an FF data byte */
	  c = 0xFF;
	} else {
	  /* Oops, it's actually a marker indicating end of compressed data.
	   * Save the marker code for later use.
	   * Fine point: it might appear that we should save the marker into
	   * bitread working state, not straight into permanent state.  But
	   * once we have hit a marker, we cannot need to suspend within the
	   * current MCU, because we will read no more bytes from the data
	   * source.  So it is OK to update permanent state right away.
	   */
	  cinfo->unread_marker = c;
	  /* See if we need to insert some fake zero bits. */
	  goto no_more_bytes;
	}
      }

      /* OK, load c into get_buffer */
      get_buffer = (get_buffer << 8) | c;
      bits_left += 8;
    } /* end while */
  } else {
  no_more_bytes:
    /* We get here if we've read the marker that terminates the compressed
     * data segment.  There should be enough bits in the buffer register
     * to satisfy the request; if so, no problem.
     */
    if (nbits > bits_left) {
      /* Uh-oh.  Report corrupted data to user and stuff zeroes into
       * the data stream, so that we can produce some kind of image.
       * We use a nonvolatile flag to ensure that only one warning message
       * appears per data segment.
       */
      if (! cinfo->entropy->insufficient_data) {
	WARNMS(cinfo, JWRN_HIT_MARKER);
	cinfo->entropy->insufficient_data = TRUE;
      }
      /* Fill the buffer with zero bits */
      get_buffer <<= MIN_GET_BITS - bits_left;
      bits_left = MIN_GET_BITS;
    }
  }

  /* Unload the local registers */
  state->next_input_byte = next_input_byte;
  state->bytes_in_buffer = bytes_in_buffer;
  state->get_buffer = get_buffer;
  state->bits_left = bits_left;

  return TRUE;
}


/* Macro version of the above, which performs much better but does not
   handle markers.  We have to hand off any blocks with markers to the
   slower routines. */

#define GET_BYTE \
{ \
  register int c0, c1; \
  c0 = GETJOCTET(*buffer++); \
  c1 = GETJOCTET(*buffer); \
  /* Pre-execute most common case */ \
  get_buffer = (get_buffer << 8) | c0; \
  bits_left += 8; \
  if (c0 == 0xFF) { \
    /* Pre-execute case of FF/00, which represents an FF data byte */ \
    buffer++; \
    if (c1 != 0) { \
      /* Oops, it's actually a marker indicating end of compressed data. */ \
      cinfo->unread_marker = c1; \
      /* Back out pre-execution and fill the buffer with zero bits */ \
      buffer -= 2; \
      get_buffer &= ~0xFF; \
    } \
  } \
}

#if __WORDSIZE == 64 || defined(_WIN64)

/* Pre-fetch 48 bytes, because the holding register is 64-bit */
#define FILL_BIT_BUFFER_FAST \
  if (bits_left < 16) { \
    GET_BYTE GET_BYTE GET_BYTE GET_BYTE GET_BYTE GET_BYTE \
  }

#else

/* Pre-fetch 16 bytes, because the holding register is 32-bit */
#define FILL_BIT_BUFFER_FAST \
  if (bits_left < 16) { \
    GET_BYTE GET_BYTE \
  }

#endif


/*
 * Out-of-line code for Huffman code decoding.
 * See jdhuff.h for info about usage.
 */

GLOBAL(int)
jpeg_huff_decode (bitread_working_state * state,
		  register bit_buf_type get_buffer, register int bits_left,
		  d_derived_tbl * htbl, int min_bits)
{
  register int l = min_bits;
  register INT32 code;

  /* HUFF_DECODE has determined that the code is at least min_bits */
  /* bits long, so fetch that many bits in one swoop. */

  CHECK_BIT_BUFFER(*state, l, return -1);
  code = GET_BITS(l);

  /* Collect the rest of the Huffman code one bit at a time. */
  /* This is per Figure F.16 in the JPEG spec. */

  while (code > htbl->maxcode[l]) {
    code <<= 1;
    CHECK_BIT_BUFFER(*state, 1, return -1);
    code |= GET_BITS(1);
    l++;
  }

  /* Unload the local registers */
  state->get_buffer = get_buffer;
  state->bits_left = bits_left;

  /* With garbage input we may reach the sentinel value l = 17. */

  if (l > 16) {
    WARNMS(state->cinfo, JWRN_HUFF_BAD_CODE);
    return 0;			/* fake a zero as the safest result */
  }

  return htbl->pub->huffval[ (int) (code + htbl->valoffset[l]) ];
}


/*
 * Figure F.12: extend sign bit.
 * On some machines, a shift and add will be faster than a table lookup.
 */

#define AVOID_TABLES
#ifdef AVOID_TABLES

#define HUFF_EXTEND(x,s)  ((x) + ((((x) - (1<<((s)-1))) >> 31) & (((-1)<<(s)) + 1)))

#else

#define HUFF_EXTEND(x,s)  ((x) < extend_test[s] ? (x) + extend_offset[s] : (x))

static const int extend_test[16] =   /* entry n is 2**(n-1) */
  { 0, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
    0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000 };

static const int extend_offset[16] = /* entry n is (-1 << n) + 1 */
  { 0, ((-1)<<1) + 1, ((-1)<<2) + 1, ((-1)<<3) + 1, ((-1)<<4) + 1,
    ((-1)<<5) + 1, ((-1)<<6) + 1, ((-1)<<7) + 1, ((-1)<<8) + 1,
    ((-1)<<9) + 1, ((-1)<<10) + 1, ((-1)<<11) + 1, ((-1)<<12) + 1,
    ((-1)<<13) + 1, ((-1)<<14) + 1, ((-1)<<15) + 1 };

#endif /* AVOID_TABLES */


/*
 * Check for a restart marker & resynchronize decoder.
 * Returns FALSE if must suspend.
 */

LOCAL(boolean)
process_restart (j_decompress_ptr cinfo)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;
  int ci;

  /* Throw away any unused bits remaining in bit buffer; */
  /* include any full bytes in next_marker's count of discarded bytes */
  cinfo->marker->discarded_bytes += entropy->bitstate.bits_left / 8;
  entropy->bitstate.bits_left = 0;

  /* Advance past the RSTn marker */
  if (! (*cinfo->marker->read_restart_marker) (cinfo))
    return FALSE;

  /* Re-initialize DC predictions to 0 */
  for (ci = 0; ci < cinfo->comps_in_scan; ci++)
    entropy->saved.last_dc_val[ci] = 0;

  /* Reset restart counter */
  entropy->restarts_to_go = cinfo->restart_interval;

  /* Reset out-of-data flag, unless read_restart_marker left us smack up
   * against a marker.  In that case we will end up treating the next data
   * segment as empty, and we can avoid producing bogus output pixels by
   * leaving the flag set.
   */
  if (cinfo->unread_marker == 0)
    entropy->pub.insufficient_data = FALSE;

  return TRUE;
}


LOCAL(boolean)
decode_mcu_slow (j_decompress_ptr cinfo, JBLOCKROW *MCU_data)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;
  BITREAD_STATE_VARS;
  int blkn;
  savable_state state;
  /* Outer loop handles each block in the MCU */

  /* Load up working state */
  BITREAD_LOAD_STATE(cinfo,entropy->bitstate);
  ASSIGN_STATE(state, entropy->saved);

  for (blkn = 0; blkn < cinfo->blocks_in_MCU; blkn++) {
    JBLOCKROW block = MCU_data[blkn];
    d_derived_tbl * dctbl = entropy->dc_cur_tbls[blkn];
    d_derived_tbl * actbl = entropy->ac_cur_tbls[blkn];
    register int s, k, r;

    /* Decode a single block's worth of coefficients */

    /* Section F.2.2.1: decode the DC coefficient difference */
    HUFF_DECODE(s, br_state, dctbl, return FALSE, label1);
    if (s) {
      CHECK_BIT_BUFFER(br_state, s, return FALSE);
      r = GET_BITS(s);
      s = HUFF_EXTEND(r, s);
    }

    if (entropy->dc_needed[blkn]) {
      /* Convert DC difference to actual value, update last_dc_val */
      int ci = cinfo->MCU_membership[blkn];
      s += state.last_dc_val[ci];
      state.last_dc_val[ci] = s;
      /* Output the DC coefficient (assumes jpeg_natural_order[0] = 0) */
      (*block)[0] = (JCOEF) s;
    }

    if (entropy->ac_needed[blkn]) {

      /* Section F.2.2.2: decode the AC coefficients */
      /* Since zeroes are skipped, output area must be cleared beforehand */
      for (k = 1; k < DCTSIZE2; k++) {
        HUFF_DECODE(s, br_state, actbl, return FALSE, label2);

        r = s >> 4;
        s &= 15;
      
        if (s) {
          k += r;
          CHECK_BIT_BUFFER(br_state, s, return FALSE);
          r = GET_BITS(s);
          s = HUFF_EXTEND(r, s);
          /* Output coefficient in natural (dezigzagged) order.
           * Note: the extra entries in jpeg_natural_order[] will save us
           * if k >= DCTSIZE2, which could happen if the data is corrupted.
           */
          (*block)[jpeg_natural_order[k]] = (JCOEF) s;
        } else {
          if (r != 15)
            break;
          k += 15;
        }
      }

    } else {

      /* Section F.2.2.2: decode the AC coefficients */
      /* In this path we just discard the values */
      for (k = 1; k < DCTSIZE2; k++) {
        HUFF_DECODE(s, br_state, actbl, return FALSE, label3);

        r = s >> 4;
        s &= 15;

        if (s) {
          k += r;
          CHECK_BIT_BUFFER(br_state, s, return FALSE);
          DROP_BITS(s);
        } else {
          if (r != 15)
            break;
          k += 15;
        }
      }
    }
  }

  /* Completed MCU, so update state */
  BITREAD_SAVE_STATE(cinfo,entropy->bitstate);
  ASSIGN_STATE(entropy->saved, state);
  return TRUE;
}


LOCAL(boolean)
decode_mcu_fast (j_decompress_ptr cinfo, JBLOCKROW *MCU_data)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;
  BITREAD_STATE_VARS;
  JOCTET *buffer;
  int blkn;
  savable_state state;
  /* Outer loop handles each block in the MCU */

  /* Load up working state */
  BITREAD_LOAD_STATE(cinfo,entropy->bitstate);
  buffer = (JOCTET *) br_state.next_input_byte;
  ASSIGN_STATE(state, entropy->saved);

  for (blkn = 0; blkn < cinfo->blocks_in_MCU; blkn++) {
    JBLOCKROW block = MCU_data[blkn];
    d_derived_tbl * dctbl = entropy->dc_cur_tbls[blkn];
    d_derived_tbl * actbl = entropy->ac_cur_tbls[blkn];
    register int s, k, r, l;

    HUFF_DECODE_FAST(s, l, dctbl);
    if (s) {
      FILL_BIT_BUFFER_FAST
      r = GET_BITS(s);
      s = HUFF_EXTEND(r, s);
    }

    if (entropy->dc_needed[blkn]) {
      int ci = cinfo->MCU_membership[blkn];
      s += state.last_dc_val[ci];
      state.last_dc_val[ci] = s;
      (*block)[0] = (JCOEF) s;
    }

    if (entropy->ac_needed[blkn]) {

      for (k = 1; k < DCTSIZE2; k++) {
        HUFF_DECODE_FAST(s, l, actbl);
        r = s >> 4;
        s &= 15;
      
        if (s) {
          k += r;
          FILL_BIT_BUFFER_FAST
          r = GET_BITS(s);
          s = HUFF_EXTEND(r, s);
          (*block)[jpeg_natural_order[k]] = (JCOEF) s;
        } else {
          if (r != 15) break;
          k += 15;
        }
      }

    } else {

      for (k = 1; k < DCTSIZE2; k++) {
        HUFF_DECODE_FAST(s, l, actbl);
        r = s >> 4;
        s &= 15;

        if (s) {
          k += r;
          FILL_BIT_BUFFER_FAST
          DROP_BITS(s);
        } else {
          if (r != 15) break;
          k += 15;
        }
      }
    }
  }

  if (cinfo->unread_marker != 0) {
    cinfo->unread_marker = 0;
    return FALSE;
  }

  br_state.bytes_in_buffer -= (buffer - br_state.next_input_byte);
  br_state.next_input_byte = buffer;
  BITREAD_SAVE_STATE(cinfo,entropy->bitstate);
  ASSIGN_STATE(entropy->saved, state);
  return TRUE;
}


/*
 * Decode and return one MCU's worth of Huffman-compressed coefficients.
 * The coefficients are reordered from zigzag order into natural array order,
 * but are not dequantized.
 *
 * The i'th block of the MCU is stored into the block pointed to by
 * MCU_data[i].  WE ASSUME THIS AREA HAS BEEN ZEROED BY THE CALLER.
 * (Wholesale zeroing is usually a little faster than retail...)
 *
 * Returns FALSE if data source requested suspension.  In that case no
 * changes have been made to permanent state.  (Exception: some output
 * coefficients may already have been assigned.  This is harmless for
 * this module, since we'll just re-assign them on the next call.)
 */

#define BUFSIZE (DCTSIZE2 * 2)

METHODDEF(boolean)
decode_mcu (j_decompress_ptr cinfo, JBLOCKROW *MCU_data)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;
  int usefast = 1;

  /* Process restart marker if needed; may have to suspend */
  if (cinfo->restart_interval) {
    if (entropy->restarts_to_go == 0)
      if (! process_restart(cinfo))
	return FALSE;
    usefast = 0;
  }

  if (cinfo->src->bytes_in_buffer < BUFSIZE * cinfo->blocks_in_MCU
    || cinfo->unread_marker != 0)
    usefast = 0;

  /* If we've run out of data, just leave the MCU set to zeroes.
   * This way, we return uniform gray for the remainder of the segment.
   */
  if (! entropy->pub.insufficient_data) {

    if (usefast) {
      if (!decode_mcu_fast(cinfo, MCU_data)) goto use_slow;
    }
    else {
      use_slow:
      if (!decode_mcu_slow(cinfo, MCU_data)) return FALSE;
    }

  }

  /* Account for restart interval (no-op if not using restarts) */
  entropy->restarts_to_go--;

  return TRUE;
}


/*
 * Module initialization routine for Huffman entropy decoding.
 */

GLOBAL(void)
jinit_huff_decoder (j_decompress_ptr cinfo)
{
  huff_entropy_ptr entropy;
  int i;

  entropy = (huff_entropy_ptr)
    (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				SIZEOF(huff_entropy_decoder));
  cinfo->entropy = (struct jpeg_entropy_decoder *) entropy;
  entropy->pub.start_pass = start_pass_huff_decoder;
  entropy->pub.decode_mcu = decode_mcu;
#ifdef ANDROID
  entropy->pub.decode_mcu_discard_coef = decode_mcu_discard_coef;
  entropy->pub.configure_huffman_decoder = configure_huffman_decoder;
  entropy->pub.get_huffman_decoder_configuration =
        get_huffman_decoder_configuration;
  entropy->pub.index = NULL;
#endif /* ANDROID */


  /* Mark tables unallocated */
  for (i = 0; i < NUM_HUFF_TBLS; i++) {
    entropy->dc_derived_tbls[i] = entropy->ac_derived_tbls[i] = NULL;
  }
}

#ifdef ANDROID

LOCAL(boolean) process_restart (j_decompress_ptr cinfo);

/*
 * Save the current Huffman deocde position and the DC coefficients
 * for each component into bitstream_offset and dc_info[], respectively.
 */
METHODDEF(void)
get_huffman_decoder_configuration(j_decompress_ptr cinfo,
        huffman_offset_data *offset)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;
  short int *dc_info = offset->prev_dc;
  int i;
  jpeg_get_huffman_decoder_configuration(cinfo, offset);
  for (i = 0; i < cinfo->comps_in_scan; i++) {
    dc_info[i] = entropy->saved.last_dc_val[i];
  }
}

/*
 * Save the current Huffman decoder position and the bit buffer
 * into bitstream_offset and get_buffer, respectively.
 */
GLOBAL(void)
jpeg_get_huffman_decoder_configuration(j_decompress_ptr cinfo,
        huffman_offset_data *offset)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;

  if (cinfo->restart_interval) {
    // We are at the end of a data segment
    if (entropy->restarts_to_go == 0)
      if (! process_restart(cinfo))
       return;
  }

  // Save restarts_to_go and next_restart_num
  offset->restarts_to_go = (unsigned short) entropy->restarts_to_go;
  offset->next_restart_num = cinfo->marker->next_restart_num;

  offset->bitstream_offset =
      (jget_input_stream_position(cinfo) << LOG_TWO_BIT_BUF_SIZE)
      + entropy->bitstate.bits_left;

  offset->get_buffer = entropy->bitstate.get_buffer;
}

/*
 * Configure the Huffman decoder to decode the image
 * starting from the bitstream position recorded in offset.
 */
METHODDEF(void)
configure_huffman_decoder(j_decompress_ptr cinfo, huffman_offset_data offset)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;
  short int *dc_info = offset.prev_dc;
  int i;
  jpeg_configure_huffman_decoder(cinfo, offset);
  for (i = 0; i < cinfo->comps_in_scan; i++) {
    entropy->saved.last_dc_val[i] = dc_info[i];
  }
}

/*
 * Configure the Huffman decoder reader position and bit buffer.
 */
GLOBAL(void)
jpeg_configure_huffman_decoder(j_decompress_ptr cinfo,
        huffman_offset_data offset)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;

  // Restore restarts_to_go and next_restart_num
  cinfo->unread_marker = 0;
  entropy->restarts_to_go = offset.restarts_to_go;
  cinfo->marker->next_restart_num = offset.next_restart_num;

  unsigned int bitstream_offset = offset.bitstream_offset;
  int blkn, i;

  unsigned int byte_offset = bitstream_offset >> LOG_TWO_BIT_BUF_SIZE;
  unsigned int bit_in_bit_buffer =
      bitstream_offset & ((1 << LOG_TWO_BIT_BUF_SIZE) - 1);

  jset_input_stream_position_bit(cinfo, byte_offset,
          bit_in_bit_buffer, offset.get_buffer);
}

/*
 * Decode one MCU's worth of Huffman-compressed coefficients.
 * The propose of this method is to calculate the
 * data length of one MCU in Huffman-coded format.
 * Therefore, all coefficients are discarded.
 */

METHODDEF(boolean)
decode_mcu_discard_coef (j_decompress_ptr cinfo)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;
  int blkn;
  BITREAD_STATE_VARS;
  savable_state state;

  /* Process restart marker if needed; may have to suspend */
  if (cinfo->restart_interval) {
    if (entropy->restarts_to_go == 0)
      if (! process_restart(cinfo))
       return FALSE;
  }

  if (! entropy->pub.insufficient_data) {

    /* Load up working state */
    BITREAD_LOAD_STATE(cinfo,entropy->bitstate);
    ASSIGN_STATE(state, entropy->saved);

    /* Outer loop handles each block in the MCU */

    for (blkn = 0; blkn < cinfo->blocks_in_MCU; blkn++) {
      d_derived_tbl * dctbl = entropy->dc_cur_tbls[blkn];
      d_derived_tbl * actbl = entropy->ac_cur_tbls[blkn];
      register int s, k, r;

      /* Decode a single block's worth of coefficients */

      /* Section F.2.2.1: decode the DC coefficient difference */
      HUFF_DECODE(s, br_state, dctbl, return FALSE, label1);
      if (s) {
       CHECK_BIT_BUFFER(br_state, s, return FALSE);
       r = GET_BITS(s);
       s = HUFF_EXTEND(r, s);
      }

      /* discard all coefficients */
      if (entropy->dc_needed[blkn]) {
       /* Convert DC difference to actual value, update last_dc_val */
       int ci = cinfo->MCU_membership[blkn];
       s += state.last_dc_val[ci];
       state.last_dc_val[ci] = s;
      }
      for (k = 1; k < DCTSIZE2; k++) {
        HUFF_DECODE(s, br_state, actbl, return FALSE, label3);

        r = s >> 4;
        s &= 15;

        if (s) {
          k += r;
          CHECK_BIT_BUFFER(br_state, s, return FALSE);
          DROP_BITS(s);
        } else {
          if (r != 15)
            break;
          k += 15;
        }
      }
    }

    /* Completed MCU, so update state */
    BITREAD_SAVE_STATE(cinfo,entropy->bitstate);
    ASSIGN_STATE(entropy->saved, state);
  }

  /* Account for restart interval (no-op if not using restarts) */
  entropy->restarts_to_go--;

  return TRUE;
}

/*
 * Call after jpeg_read_header
 */
GLOBAL(void)
jpeg_create_huffman_index(j_decompress_ptr cinfo, huffman_index *index)
{
  int i, s;
  index->scan_count = 1;
  index->total_iMCU_rows = cinfo->total_iMCU_rows;
  index->scan = (huffman_scan_header*)malloc(index->scan_count
          * sizeof(huffman_scan_header));
  index->scan[0].offset = (huffman_offset_data**)malloc(cinfo->total_iMCU_rows
          * sizeof(huffman_offset_data*));
  index->scan[0].prev_MCU_offset.bitstream_offset = 0;
  index->MCU_sample_size = DEFAULT_MCU_SAMPLE_SIZE;

  index->mem_used = sizeof(huffman_scan_header)
      + cinfo->total_iMCU_rows * sizeof(huffman_offset_data*);
}

GLOBAL(void)
jpeg_destroy_huffman_index(huffman_index *index)
{
    int i, j;
    for (i = 0; i < index->scan_count; i++) {
        for(j = 0; j < index->total_iMCU_rows; j++) {
            free(index->scan[i].offset[j]);
        }
        free(index->scan[i].offset);
    }
    free(index->scan);
}

/*
 * Set the reader byte position to offset
 */
GLOBAL(void)
jset_input_stream_position(j_decompress_ptr cinfo, int offset)
{
  if (cinfo->src->seek_input_data) {
    cinfo->src->seek_input_data(cinfo, offset);
  } else {
    cinfo->src->bytes_in_buffer = cinfo->src->current_offset - offset;
    cinfo->src->next_input_byte = cinfo->src->start_input_byte + offset;
  }
}

/*
 * Set the reader byte position to offset and bit position to bit_left
 * with bit buffer set to buf.
 */
GLOBAL(void)
jset_input_stream_position_bit(j_decompress_ptr cinfo,
        int byte_offset, int bit_left, INT32 buf)
{
  huff_entropy_ptr entropy = (huff_entropy_ptr) cinfo->entropy;

  entropy->bitstate.bits_left = bit_left;
  entropy->bitstate.get_buffer = buf;

  jset_input_stream_position(cinfo, byte_offset);
}

/*
 * Get the current reader byte position.
 */
GLOBAL(int)
jget_input_stream_position(j_decompress_ptr cinfo)
{
  return cinfo->src->current_offset - cinfo->src->bytes_in_buffer;
}

#endif /* ANDROID */
