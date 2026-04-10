/*
 * ============================================================
 * FILE: argument_parsing.c
 * ------------------------------------------------------------
 * PURPOSE:
 *   Implements command-line argument parsing and validation for
 *   the multi-process word transportation system.
 *
 * DESIGN DECISIONS:
 *   - Uses POSIX getopt() so the flags can appear in any order.
 *   - Every parameter is mandatory; missing any single one is
 *     treated as a fatal error (usage message + return -1).
 *   - All numeric values are validated for their minimum bound
 *     (>= 1) after parsing; the input file is checked for
 *     read-accessibility with access(R_OK).
 *   - String parameters (file paths) are safely copied with
 *     strncpy + explicit NUL-termination to prevent overflows.
 * ============================================================
 */

#include "argument_parsing.h"
#include <getopt.h>   /* getopt(), optarg, opterr */

/* ============================================================
 * print_usage
 * ------------------------------------------------------------
 * Prints a concise usage summary to stderr so the user knows
 * which flags are required and what they control.
 * Called automatically by parse_args() on any error.
 * ============================================================ */
void print_usage(const char *prog_name) {
    fprintf(stderr,
            "Usage: %s -f <num_floors> -w <word_carriers> -l <letter_carriers> "
            "-s <sorting_procs> -c <floor_capacity> -d <delivery_cap> "
            "-r <reposition_cap> -i <input_file> -o <output_file>\n",
            prog_name);

    /* Describe each flag so the user does not have to guess. */
    fprintf(stderr, "  -f  Number of floors (>= 1)\n");
    fprintf(stderr, "  -w  Word-carrier processes per floor (>= 1)\n");
    fprintf(stderr, "  -l  Letter-carrier processes per floor (>= 1)\n");
    fprintf(stderr, "  -s  Sorting processes per floor (>= 1)\n");
    fprintf(stderr, "  -c  Max active words per floor (>= 1)\n");
    fprintf(stderr, "  -d  Delivery elevator capacity (>= 1)\n");
    fprintf(stderr, "  -r  Reposition elevator capacity (>= 1)\n");
    fprintf(stderr, "  -i  Input file path\n");
    fprintf(stderr, "  -o  Output file path\n");
}

/* ============================================================
 * parse_args
 * ------------------------------------------------------------
 * Parses all mandatory command-line flags into *config.
 *
 * Return value:
 *    0  – success; *config is fully populated and validated.
 *   -1  – failure; a message has been written to stderr.
 * ============================================================ */
int parse_args(int argc, char *argv[], SystemConfig *config) {
    int opt;

    /*
     * Presence flags: each is set to 1 when the corresponding
     * command-line option is seen.  After the loop we verify that
     * ALL of them are 1 (every parameter is mandatory).
     */
    int has_f = 0, has_w = 0, has_l = 0, has_s = 0;
    int has_c = 0, has_d = 0, has_r = 0, has_i = 0, has_o = 0;

    /* Zero-initialise config so no field is left with garbage. */
    memset(config, 0, sizeof(SystemConfig));

    /*
     * getopt() loop – processes one flag per iteration.
     * The colon after each letter means the flag takes an argument.
     * "f:w:l:s:c:d:r:i:o:" → all nine flags require a value.
     */
    while ((opt = getopt(argc, argv, "f:w:l:s:c:d:r:i:o:")) != -1) {
        switch (opt) {

            /* -f <num_floors> ─────────────────────────────── */
            case 'f':
                config->num_floors = atoi(optarg);
                has_f = 1;
                break;

            /* -w <word_carriers_per_floor> ─────────────────── */
            case 'w':
                config->word_carriers_per_floor = atoi(optarg);
                has_w = 1;
                break;

            /* -l <letter_carriers_per_floor> ───────────────── */
            case 'l':
                config->letter_carriers_per_floor = atoi(optarg);
                has_l = 1;
                break;

            /* -s <sorting_processes_per_floor> ─────────────── */
            case 's':
                config->sorting_processes_per_floor = atoi(optarg);
                has_s = 1;
                break;

            /* -c <max_words_per_floor> ─────────────────────── */
            case 'c':
                config->max_words_per_floor = atoi(optarg);
                has_c = 1;
                break;

            /* -d <delivery_elevator_capacity> ──────────────── */
            case 'd':
                config->delivery_elevator_capacity = atoi(optarg);
                has_d = 1;
                break;

            /* -r <reposition_elevator_capacity> ────────────── */
            case 'r':
                config->reposition_elevator_capacity = atoi(optarg);
                has_r = 1;
                break;

            /*
             * -i <input_file>
             * strncpy + explicit NUL ensures we cannot overflow
             * the fixed-size buffer even with a very long path.
             */
            case 'i':
                strncpy(config->input_file, optarg,
                        sizeof(config->input_file) - 1);
                config->input_file[sizeof(config->input_file) - 1] = '\0';
                has_i = 1;
                break;

            /* -o <output_file> ─────────────────────────────── */
            case 'o':
                strncpy(config->output_file, optarg,
                        sizeof(config->output_file) - 1);
                config->output_file[sizeof(config->output_file) - 1] = '\0';
                has_o = 1;
                break;

            /*
             * Unknown flag or missing argument:
             * getopt() already printed "invalid option" to stderr.
             * Print our full usage message and signal failure.
             */
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    /* ── Check completeness ─────────────────────────────────── */
    /*
     * If any mandatory flag was not provided, reject the invocation.
     * We check all nine flags in a single condition for brevity.
     */
    if (!has_f || !has_w || !has_l || !has_s ||
        !has_c || !has_d || !has_r || !has_i || !has_o) {
        fprintf(stderr, "Error: All parameters are mandatory.\n");
        print_usage(argv[0]);
        return -1;
    }

    /* ── Numeric range validation ───────────────────────────── */
    /*
     * Each parameter must be at least 1.
     * A value of 0 or negative would cause undefined behaviour
     * when used to size arrays or as loop bounds.
     */

    if (config->num_floors < 1) {
        fprintf(stderr, "Error: num_floors must be >= 1\n");
        return -1;
    }
    if (config->word_carriers_per_floor < 1) {
        fprintf(stderr, "Error: word_carriers_per_floor must be >= 1\n");
        return -1;
    }
    if (config->letter_carriers_per_floor < 1) {
        fprintf(stderr, "Error: letter_carriers_per_floor must be >= 1\n");
        return -1;
    }
    if (config->sorting_processes_per_floor < 1) {
        fprintf(stderr, "Error: sorting_processes_per_floor must be >= 1\n");
        return -1;
    }
    if (config->max_words_per_floor < 1) {
        fprintf(stderr, "Error: max_words_per_floor must be >= 1\n");
        return -1;
    }
    if (config->delivery_elevator_capacity < 1) {
        fprintf(stderr, "Error: delivery_elevator_capacity must be >= 1\n");
        return -1;
    }
    if (config->reposition_elevator_capacity < 1) {
        fprintf(stderr, "Error: reposition_elevator_capacity must be >= 1\n");
        return -1;
    }

    /* ── Input file accessibility check ─────────────────────── */
    /*
     * access(path, R_OK) returns 0 if the calling process can read
     * the file right now.  We check this early so the user gets a
     * clear error message instead of a cryptic fopen() failure
     * later inside read_input_file().
     */
    if (access(config->input_file, R_OK) == -1) {
        fprintf(stderr, "Error: Cannot read input file '%s': %s\n",
                config->input_file, strerror(errno));
        return -1;
    }

    /* All checks passed – config is ready for use. */
    return 0;
}
