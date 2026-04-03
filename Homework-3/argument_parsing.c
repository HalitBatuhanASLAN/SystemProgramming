#include "argument_parsing.h"
#include <getopt.h>

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s -f <num_floors> -w <word_carriers> -l <letter_carriers> "
                     "-s <sorting_procs> -c <floor_capacity> -d <delivery_cap> "
                     "-r <reposition_cap> -i <input_file> -o <output_file>\n", prog_name);
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

int parse_args(int argc, char *argv[], SystemConfig *config) {
    int opt;
    /* Hangi parametreler verildi takibi */
    int has_f = 0, has_w = 0, has_l = 0, has_s = 0;
    int has_c = 0, has_d = 0, has_r = 0, has_i = 0, has_o = 0;

    /* Varsayilan degerleri sifirla */
    memset(config, 0, sizeof(SystemConfig));

    while ((opt = getopt(argc, argv, "f:w:l:s:c:d:r:i:o:")) != -1) {
        switch (opt) {
            case 'f':
                config->num_floors = atoi(optarg);
                has_f = 1;
                break;
            case 'w':
                config->word_carriers_per_floor = atoi(optarg);
                has_w = 1;
                break;
            case 'l':
                config->letter_carriers_per_floor = atoi(optarg);
                has_l = 1;
                break;
            case 's':
                config->sorting_processes_per_floor = atoi(optarg);
                has_s = 1;
                break;
            case 'c':
                config->max_words_per_floor = atoi(optarg);
                has_c = 1;
                break;
            case 'd':
                config->delivery_elevator_capacity = atoi(optarg);
                has_d = 1;
                break;
            case 'r':
                config->reposition_elevator_capacity = atoi(optarg);
                has_r = 1;
                break;
            case 'i':
                strncpy(config->input_file, optarg, sizeof(config->input_file) - 1);
                config->input_file[sizeof(config->input_file) - 1] = '\0';
                has_i = 1;
                break;
            case 'o':
                strncpy(config->output_file, optarg, sizeof(config->output_file) - 1);
                config->output_file[sizeof(config->output_file) - 1] = '\0';
                has_o = 1;
                break;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    /* Tum zorunlu parametreler verildi mi kontrol et */
    if (!has_f || !has_w || !has_l || !has_s || !has_c || !has_d || !has_r || !has_i || !has_o) {
        fprintf(stderr, "Error: All parameters are mandatory.\n");
        print_usage(argv[0]);
        return -1;
    }

    /* Deger validasyonlari */
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

    /* Input dosyasi var mi kontrol et */
    if (access(config->input_file, R_OK) == -1) {
        fprintf(stderr, "Error: Cannot read input file '%s': %s\n",
                config->input_file, strerror(errno));
        return -1;
    }

    return 0;
}