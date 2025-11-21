/* Minimal getopt implementation for Windows */
#include "getopt.h"
#include <string.h>
#include <stdio.h>

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

static char *nextchar = NULL;

int getopt(int argc, char *const argv[], const char *optstring) {
    if (optind >= argc || argv[optind] == NULL || argv[optind][0] != '-' || argv[optind][1] == '\0') {
        return -1;
    }

    if (strcmp(argv[optind], "--") == 0) {
        optind++;
        return -1;
    }

    if (nextchar == NULL || *nextchar == '\0') {
        nextchar = argv[optind] + 1;
    }

    optopt = *nextchar++;
    const char *p = strchr(optstring, optopt);

    if (p == NULL || optopt == ':') {
        if (opterr && *optstring != ':') {
            fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], optopt);
        }
        if (*nextchar == '\0') {
            optind++;
            nextchar = NULL;
        }
        return '?';
    }

    if (p[1] == ':') {
        if (*nextchar != '\0') {
            optarg = nextchar;
            optind++;
            nextchar = NULL;
        } else if (++optind < argc) {
            optarg = argv[optind++];
        } else {
            if (opterr && *optstring != ':') {
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], optopt);
            }
            return (p[2] == ':') ? optopt : '?';
        }
    } else {
        optarg = NULL;
        if (*nextchar == '\0') {
            optind++;
            nextchar = NULL;
        }
    }

    return optopt;
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    if (optind >= argc || argv[optind] == NULL) {
        return -1;
    }

    if (argv[optind][0] != '-') {
        return -1;
    }

    if (strcmp(argv[optind], "--") == 0) {
        optind++;
        return -1;
    }

    /* Handle long options */
    if (argv[optind][0] == '-' && argv[optind][1] == '-') {
        const char *name = argv[optind] + 2;
        const char *eq = strchr(name, '=');
        size_t namelen = eq ? (size_t)(eq - name) : strlen(name);

        for (int i = 0; longopts[i].name != NULL; i++) {
            if (strncmp(name, longopts[i].name, namelen) == 0 &&
                strlen(longopts[i].name) == namelen) {
                
                if (longindex != NULL) {
                    *longindex = i;
                }

                optind++;

                if (longopts[i].has_arg == required_argument) {
                    if (eq != NULL) {
                        optarg = (char *)(eq + 1);
                    } else if (optind < argc) {
                        optarg = argv[optind++];
                    } else {
                        if (opterr) {
                            fprintf(stderr, "%s: option '--%s' requires an argument\n",
                                    argv[0], longopts[i].name);
                        }
                        return '?';
                    }
                } else if (longopts[i].has_arg == optional_argument) {
                    optarg = eq ? (char *)(eq + 1) : NULL;
                } else {
                    optarg = NULL;
                }

                if (longopts[i].flag != NULL) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }

        if (opterr) {
            fprintf(stderr, "%s: unrecognized option '--%.*s'\n",
                    argv[0], (int)namelen, name);
        }
        optind++;
        return '?';
    }

    /* Handle short options */
    return getopt(argc, argv, optstring);
}
