/*******************************************************************************
 *
 *  = cli.c
 *
 *  Entry point for the 'bitcoin-iterate' command-line program.
 *
 *  Format strings are set by command-line flags.  These format
 *  strings are then used by data-structure-appropriate functions
 *  which delegate to the `print_format` function.
 *
 *  UTXOs are only collected if certain format codes were specified.
 *
 */
#include <ccan/err/err.h>
#include <ccan/opt/opt.h>
#include "iterate.h"
#include "utils.h"
#include "format.h"

static char *blockfmt = NULL, *txfmt = NULL, *inputfmt = NULL, *outputfmt = NULL, *utxofmt = NULL;

static void print_block(const struct utxo_map *utxo_map, struct block *b)
{
  print_format(blockfmt, utxo_map, b, NULL, 0, NULL, NULL, NULL);
}
static void print_transaction(const struct utxo_map *utxo_map, struct block *b, struct bitcoin_transaction *t, size_t txnum)
{
  print_format(txfmt, utxo_map, b, t, txnum, NULL, NULL, NULL);
}
static void print_input(const struct utxo_map *utxo_map, struct block *b, struct bitcoin_transaction *t, size_t txnum, struct bitcoin_transaction_input *i)
{
  print_format(inputfmt, utxo_map, b, t, txnum, i, NULL, NULL);
}
static void print_output(const struct utxo_map *utxo_map, struct block *b, struct bitcoin_transaction *t, size_t txnum, struct bitcoin_transaction_output *o)
{
  print_format(outputfmt, utxo_map, b, t, txnum, NULL, o, NULL);
}
static void print_utxo(const struct utxo_map *utxo_map, struct block *b, struct utxo *u)
{
  print_format(utxofmt, utxo_map, b, NULL, 0, NULL, NULL, u);
}

/* static block_function       blockfn  = &print_block; */
/* static transaction_function txfn     = &print_transaction; */
/* static input_function       inputfn  = &print_input; */
/* static output_function      outputfn = &print_output; */
/* static utxo_function        utxofn   = &print_utxo; */

int main(int argc, char *argv[])
{
  char *blockdir = NULL, *cachedir = NULL;
  bool use_testnet = false;
  unsigned long block_start = 0, block_end = -1UL;
  u8 tip[SHA256_DIGEST_LENGTH] = { 0 }, start_hash[SHA256_DIGEST_LENGTH] = { 0 };
  bool needs_utxo = false;
  unsigned int utxo_period = 144;
  bool use_mmap = true;
  unsigned progress_marks = 0;
  bool quiet = false;

  err_set_progname(argv[0]);
  opt_register_noarg("-h|--help", opt_usage_and_exit,
		     "\nValid block, transaction, input, output, and utxo format:\n"
		     "  <literal>: unquoted\n"
		     "  %bl: block length\n"
		     "  %bv: block version\n"
		     "  %bp: block prev hash\n"
		     "  %bm: block merkle hash\n"
		     "  %bs: block timestamp\n"
		     "  %bt: block target\n"
		     "  %bn: block nonce\n"
		     "  %bc: block transaction count\n"
		     "  %bh: block hash\n"
		     "  %bN: block height\n"
		     "  %bH: block header (hex string)\n"
		     "Valid transaction, input or output format:\n"
		     "  %th: transaction hash\n"
		     "  %tv: transaction version\n"
		     "  %ti: transaction input count\n"
		     "  %to: transaction output count\n"
		     "  %tt: transaction locktime\n"
		     "  %tl: transaction length\n"
		     "  %tN: transaction number\n"
		     "  %tF: transaction fee paid\n"
		     "  %tD: transaction bitcoin days destroyed\n"
		     "  %tX: transaction in hex\n"
		     "Valid input format:\n"
		     "  %ia: input amount\n"
		     "  %ih: input hash\n"
		     "  %ii: input index\n"
		     "  %il: input script length\n"
		     "  %is: input script as a hex string\n"
		     "  %iN: input number\n"
		     "  %iX: input in hex\n"
		     "  %iB: input UTXO block number (0 for coinbase)\n"
		     "  %ip: input payment guess: same ("
		     stringify(CHANGE_OUTPUT) ") or different ("
		     stringify(PAYMENT_OUTPUT) ") owner, or ("
		     stringify(UNKNOWN_OUTPUT) ") unknown\n"
		     "Valid output format:\n"
		     "  %oa: output amount\n"
		     "  %ol: output script length\n"
		     "  %os: output script as a hex string\n"
		     "  %oN: output number\n"
		     "  %oU: output is unspendable (0 if spendable)\n"
		     "  %oX: output in hex\n"
		     "Valid utxo format:\n"
		     "  %uh: utxo transaction hash\n"
		     "  %ut: utxo timestamp\n"
		     "  %uc: utxo output count\n"
		     "  %uu: utxo unspent output count\n"
		     "  %us: utxo spent output count\n"
		     "  %uU: utxo unspent amount\n"
		     "  %uS: utxo spent amount\n"
		     "  %uC: utxo bitcoin days created\n",
		     "Display help message");
  opt_register_arg("--block", opt_set_charp, NULL, &blockfmt,
		   "Format to print for each block");
  opt_register_arg("--tx|--transaction", opt_set_charp, NULL, &txfmt,
		   "Format to print for each transaction");
  opt_register_arg("--input", opt_set_charp, NULL, &inputfmt,
		   "Format to print for each transaction input");
  opt_register_arg("--output", opt_set_charp, NULL, &outputfmt,
		   "Format to print for each transaction output");
  opt_register_arg("--utxo", opt_set_charp, NULL, &utxofmt,
		   "Format to print for each UTXO");
  opt_register_arg("--utxo-period", opt_set_uintval, NULL,
		   &utxo_period, "Loop over UTXOs every this many blocks");
  opt_register_arg("--progress", opt_set_uintval, NULL,
		   &progress_marks, "Print . to stderr this many times");
  opt_register_noarg("--no-mmap", opt_set_invbool, &use_mmap,
		     "Don't mmap the block files");
  opt_register_noarg("--quiet|-q", opt_set_bool, &quiet,
		     "Don't output progress information");
  opt_register_noarg("--testnet|-t", opt_set_bool, &use_testnet,
		     "Look for testnet3 blocks");
  opt_register_arg("--blockdir", opt_set_charp, NULL, &blockdir,
		   "Block directory instead of ~/.bitcoin/[testnet3/]blocks");
  opt_register_arg("--end-hash", opt_set_hash, NULL, tip,
		   "Best blockhash to use instead of longest chain.");
  opt_register_arg("--start-hash", opt_set_hash, NULL, start_hash,
		   "Blockhash to start at instead of genesis.");
  opt_register_arg("--start", opt_set_ulongval, NULL, &block_start,
		   "Block number to start instead of genesis.");
  opt_register_arg("--end", opt_set_ulongval, NULL, &block_end,
		   "Block number to end at instead of longest chain.");
  opt_register_arg("--cache", opt_set_charp, NULL, &cachedir,
		   "Cache for multiple runs.");
  opt_parse(&argc, argv, opt_log_stderr_exit);

  if (argc != 1)
    opt_usage_and_exit(NULL);

  if (txfmt && strstr(txfmt, "%tF"))
    needs_utxo = true;
  if (txfmt && strstr(txfmt, "%tD"))
    needs_utxo = true;
  if (inputfmt && strstr(inputfmt, "%tF"))
    needs_utxo = true;
  if (inputfmt && strstr(inputfmt, "%tD"))
    needs_utxo = true;
  if (inputfmt && strstr(inputfmt, "%iB"))
    needs_utxo = true;
  if (inputfmt && strstr(inputfmt, "%ia"))
    needs_utxo = true;
  if (inputfmt && strstr(inputfmt, "%ip"))
    needs_utxo = true;
  if (outputfmt && strstr(outputfmt, "%tF"))
    needs_utxo = true;
  if (outputfmt && strstr(outputfmt, "%tD"))
    needs_utxo = true;
  if (utxofmt)
    needs_utxo = true;
  
  iterate(blockdir, cachedir,
	  use_testnet,
	  block_start, block_end, start_hash, tip,
	  needs_utxo, utxo_period,
	  use_mmap,
	  progress_marks, quiet,
	  /* blockfn, txfn, inputfn, outputfn, utxofn); */
	  print_block, print_transaction, print_input, print_output, print_utxo);  
  return 0;
}
