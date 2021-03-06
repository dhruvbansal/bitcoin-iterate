#include <sys/types.h>
#include <inttypes.h>
#include <assert.h>
#include <ccan/err/err.h>
#include "calculations.h"

/* This is kind of silly, since they can print it and sum it
 * themselves.  But convenient though... */
s64 calculate_fees(const struct utxo_map *utxo_map,
		   const struct transaction *t,
		   bool is_coinbase)
{
  size_t i;
  s64 total = 0;

  if (is_coinbase)
    goto sum_outputs;
	
  for (i = 0; i < t->input_count; i++) {
    struct utxo *utxo;

    utxo = utxo_map_get(utxo_map, t->input[i].hash);
    if (!utxo)
      errx(1, "Unknown utxo for "SHA_FMT,
	   SHA_VALS(t->input[i].hash));

    if (t->input[i].index >= utxo->num_outputs)
      errx(1, "Invalid utxo output %u for "SHA_FMT,
	   t->input[i].index, SHA_VALS(t->input[i].hash));
    total += utxo->amount[t->input[i].index];
  }

 sum_outputs:
  for (i = 0; i < t->output_count; i++)
    total -= t->output[i].amount;

  if (!is_coinbase && total < 0)
    errx(1, "Invalid total %"PRIi64" for "SHA_FMT,
	 total, SHA_VALS(t->sha256));
  return total;
}

#define MAXU32 (0xFFFFFFFFul)
static void mul_and_add(u64 *over, u64 *base, u64 l, u64 r)
{
  u64 l_h = l >> 32,
    r_h = r >> 32,
    l_l = l & MAXU32,
    r_l = r & MAXU32;
  u64 a = *over, b = 0, c = *base;
  b = (c >> 32); c &= MAXU32;

  assert (0 <= b && b <= MAXU32);
  assert (0 <= c && c <= MAXU32);

  c += l_l * r_l;
  b += (c >> 32); c &= MAXU32;
  a += (b >> 32); b &= MAXU32;

  b += l_h * r_l;
  a += (b >> 32); b &= MAXU32;

  b += l_l * r_h;
  a += (b >> 32); b &= MAXU32;

  a += l_h * r_h;  /* realistically r_h=0 so this is a no op */

  *over = a;
  *base = (b << 32) | c;
}

s64 calculate_bdc(const struct utxo *u, struct block *current_block, struct block *last_utxo_block)
{
  u32 interval = (last_utxo_block ? (current_block->bh.timestamp - last_utxo_block->bh.timestamp) : 0);
  u32 utxo_age = (current_block->bh.timestamp - u->timestamp);
  u64 total_over = 0;
  u64 total_base = 0;
  mul_and_add(&total_over, &total_base, u->unspent, (utxo_age <= interval) ? utxo_age : interval);
  /* we have satoshi-seconds, convert to satoshi days by dividing by */
  /* 86400 */
  if (total_over >= 86400/2)
    return -2; /* overflow! */
  return (((total_over << 47) / 86400) << 17) + (total_base / 86400);
}

s64 calculate_bdd(const struct utxo_map *utxo_map,
		  const struct transaction *t,
		  bool is_coinbase, u32 timestamp)
{
  size_t i;
  u64 total_over = 0;
  u64 total_base = 0;

  if (is_coinbase)
    return 0;

  for (i = 0; i < t->input_count; i++) {
    struct utxo *utxo;

    utxo = utxo_map_get(utxo_map, t->input[i].hash);
    if (!utxo)
      errx(1, "Unknown utxo for "SHA_FMT,
	   SHA_VALS(t->input[i].hash));

    if (t->input[i].index >= utxo->num_outputs)
      errx(1, "Invalid utxo output %u for "SHA_FMT,
	   t->input[i].index, SHA_VALS(t->input[i].hash));


    mul_and_add(&total_over, &total_base,
		utxo->amount[t->input[i].index],
		timestamp > utxo->timestamp ? timestamp - utxo->timestamp : 0);
  }

  /* we have satoshi-seconds, convert to satoshi days by dividing by
   * 86400 */
  if (total_over >= 86400/2)
    return -2; /* overflow! */
  return (((total_over << 47) / 86400) << 17) + (total_base / 86400);
}

#define BTCS_PER_SATOSHI 1E-8

double to_btc(s64 satoshis)
{
  return ((double) ((s64) satoshis)) * BTCS_PER_SATOSHI;
}
