/*
 * ============================================================
 * FILE: argument_parsing.h
 * ------------------------------------------------------------
 * PURPOSE:
 *   Public interface for the command-line argument parsing
 *   module.  Any source file that needs to call parse_args()
 *   or print_usage() must include this header.
 *
 * HOW IT FITS INTO THE SYSTEM:
 *   main() calls parse_args() as the very first step before
 *   any processes are created or shared memory is allocated.
 *   If parse_args() returns -1 the program exits immediately
 *   with a non-zero status code and a usage message on stderr.
 * ============================================================
 */

#ifndef ARGUMENT_PARSING_H
#define ARGUMENT_PARSING_H

#include "common.h"   /* Brings in SystemConfig and standard headers */

/*
 * parse_args – parse and validate all mandatory command-line flags.
 *
 * Parameters:
 *   argc   – argument count forwarded from main().
 *   argv   – argument vector forwarded from main().
 *   config – output parameter; filled with validated values on success.
 *
 * Returns:
 *    0  on success  (config is fully populated and validated).
 *   -1  on any error (missing flag, out-of-range value, unreadable
 *       input file, …); a descriptive message is written to stderr
 *       and print_usage() is called automatically.
 *
 * Mandatory flags (all must be present):
 *   -f <num_floors>              Number of building floors (>= 1)
 *   -w <word_carriers_per_floor> Word-carrier processes per floor (>= 1)
 *   -l <letter_carriers_per_floor> Letter-carrier processes per floor (>= 1)
 *   -s <sorting_procs_per_floor> Sorting processes per floor (>= 1)
 *   -c <max_words_per_floor>     Floor capacity in active words (>= 1)
 *   -d <delivery_cap>            Delivery elevator capacity (>= 1)
 *   -r <reposition_cap>          Reposition elevator capacity (>= 1)
 *   -i <input_file>              Path to readable input .txt file
 *   -o <output_file>             Path for the output .txt file
 */
int parse_args(int argc, char *argv[], SystemConfig *config);

/*
 * print_usage – print a formatted usage/help message to stderr.
 *
 * Called automatically by parse_args() on error, but can also be
 * called directly (e.g., on -h / --help if desired).
 *
 * Parameters:
 *   prog_name – argv[0], used to show the correct executable name
 *               in the usage line.
 */
void print_usage(const char *prog_name);

#endif /* ARGUMENT_PARSING_H */
