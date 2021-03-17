#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <mpi.h>
#include <errno.h>

#ifndef DEFAULT_BALL_SIZE
#define DEFAULT_BALL_SIZE 8192
#endif

#include <getopt.h>

static struct option cli_options[] = {
        { "help",       no_argument,        0,  'h' },
        { "verbose",    no_argument,        0,  'v' },
        { "size",       required_argument,  0,  's' },
        { "rounds",     required_argument,  0,  'r' },
        { "method",     required_argument,  0,  'm' },
        { "outfile",    required_argument,  0,  'o' },
        { "root-rank",  required_argument,  0,  'R' },
        { NULL,         0,                  0,   0  }
    };
const char *cli_options_str = "hvs:r:m:o:R:";

//

const char* bounce_method_names[] = {
        "sendrecv",
        "broadcast",
        NULL
    };

typedef enum {
    bounce_method_sendrecv = 0,
    bounce_method_broadcast,
    bounce_method_max,
    
    bounce_method_default = bounce_method_sendrecv
} bounce_method_t;

bounce_method_t
bounce_method_parse(
    const char  *method_str
)
{
    bounce_method_t method = bounce_method_sendrecv;
    
    while ( method < bounce_method_max ) {
        if ( strcasecmp(method_str, bounce_method_names[method]) == 0 ) break;
        method++;
    }
    return method;
}

//

typedef enum {
    verbosity_error = 0,
    verbosity_warning,
    verbosity_info,
    verbosity_debug,
    verbosity_max
} verbosity_t;

//

verbosity_t         gVerbosity = verbosity_error;
int                 gEarlyTermination = 0;
int                 gRootRank = 0;
int                 gMyRank = -1;
size_t              gSize = DEFAULT_BALL_SIZE;
long long           gRounds = -1;
bounce_method_t     gMethod = bounce_method_default;

//

void
logline(
    int         level,
    const char  *format,
    ...
)
{
    if ( level <= gVerbosity ) {
        va_list     vargs;
    
        va_start(vargs, format);
        vfprintf(stderr, format, vargs);
        va_end(vargs);
        fflush(stderr);
    }
}

#define ERROR(FMT, ...)     logline(verbosity_error,    "[ ERROR ] " FMT "\n", ##__VA_ARGS__)
#define WARNING(FMT, ...)   logline(verbosity_warning,  "[WARNING] " FMT "\n", ##__VA_ARGS__)
#define INFO(FMT, ...)      logline(verbosity_info,     "[ INFO  ] " FMT "\n", ##__VA_ARGS__)
#define DEBUG(FMT, ...)     logline(verbosity_debug,    "[ DEBUG ] " FMT "\n", ##__VA_ARGS__)

//

void
usage(
    const char  *exe
)
{
    bounce_method_t     method = bounce_method_sendrecv;
    
    printf(
            "usage:\n\n"
            "    %s {options}\n\n"
            "  options:\n\n"
            "    -h/--help                  show this information\n"
            "    -v/--verbose               increase amount of information displayed by root rank\n"
            "    -r/--root-rank #           which rank should handle output and start the ball\n"
            "                               rolling (default: 0)\n"
            "    -o/--outfile <path>        file to which all output should be written (not including\n"
            "                               errors, warnings, info, and debug output)\n"
            "    -m/--method <method>       method used to pass the ball (default: %s)\n"
            "    -s/--size <byte-size>      size of the ball (default: %lld)\n"
            "    -r/--rounds #              number of rounds to pass the ball; use a negative integer\n"
            "                               to run indefinitely, zero to setup the run and exit before\n"
            "                               passing the ball\n"
            "\n"
            "    <byte-size> := #{.#}{TGMK{i}{B}}\n"
            "    <method> := ",
            exe,
            bounce_method_names[bounce_method_default],
            (long long)DEFAULT_BALL_SIZE
        );
    while ( method < bounce_method_max ) {
        printf("%s%s", (method != bounce_method_sendrecv) ? ", " : "", bounce_method_names[method]);
        method++;
    }
    printf("\n\n");
}

//

void
catch_SIGUSR2(
    int         sigMask,
    siginfo_t*  sigInfo,
    void*       context
)
{
    WARNING("[rank %d] catch_SIGUSR2", gMyRank);
    gEarlyTermination = sigMask;
}

//

size_t
parse_memory(
    const char          *memory_str
)
{
    char                *endptr;
    unsigned long long  value;
    
    value = strtoull(memory_str, &endptr, 0);
    if ( endptr && (endptr > memory_str) ) {
        unsigned long   base = 1000, magnitude = 0;
    
        while ( *endptr && isspace(*endptr) ) endptr++;
        if ( endptr ) {
            switch ( *endptr ) {
                case 't':
                case 'T':
                    magnitude++;
                case 'g':
                case 'G':
                    magnitude++;
                case 'm':
                case 'M':
                    magnitude++;
                case 'k':
                case 'K':
                    magnitude++;
                    endptr++;
                    break;
            }
            switch ( *endptr ) {
                case 'i':
                case 'I':
                    base = 1024;
                    endptr++;
                    break;
            }
            switch ( *endptr ) {
                case 'b':
                case 'B':
                    endptr++;
                    break;
            }
            if ( *endptr ) {
                value = 0;
            } else {
                while ( magnitude-- ) value *= base;
            }
        }
    } else {
        value = 0;
    }
    return value;
}

//

int
main(
    int                 argc,
    char*               argv[]
)
{
    int                 rank, size, rc, optch;
    long long           round = 0, dummy_lld;
    long                dummy_ld;
    void                *the_ball = NULL;
    MPI_Status          status;
    FILE*               out_fptr = stdout;
    const char          *out_filename = NULL;
    char                *endptr;
    struct sigaction    newSIGUSR2 = {
                                .sa_handler = NULL,
                                .sa_sigaction = catch_SIGUSR2,
                                .sa_mask = 0,
                                .sa_flags = SA_SIGINFO,
                                .sa_restorer = NULL
                              };
    
    DEBUG("calling MPI_Init_thread()");
    MPI_Init_thread(&argc, (char***)&argv, MPI_THREAD_MULTIPLE, &rc);
    
    DEBUG("calling MPI_Comm_rank()");
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    
    DEBUG("calling MPI_Comm_size()");
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    
    //
    // Parse CLI arguments
    //
    while ( (optch = getopt_long(argc, argv, cli_options_str, cli_options, NULL)) != -1 ) {
        switch ( optch ) {
            case 'v':
                if ( ++gVerbosity == verbosity_max ) {
                    gVerbosity = verbosity_max - 1;
                } else {
                    DEBUG("verbosity increased to %d", gVerbosity);
                }
                break;
            case 'h':
                usage(argv[0]);
                MPI_Finalize();
                exit(0);
                break;
            case 's':
                gSize = parse_memory(optarg);
                if ( gSize == 0 ) {
                    ERROR("invalid memory size provided to -s/--size: %s", optarg);
                    MPI_Finalize();
                    exit(EINVAL);
                }
                DEBUG("ball will be %llu bytes in size", gSize);
                break;
            case 'r':
                dummy_lld = strtoll(optarg, &endptr, 0);
                if ( endptr && (endptr > optarg) ) {
                    gRounds = dummy_lld;
                    DEBUG("round count set to %lld", gRounds);
                } else {
                    ERROR("invalid round count provided to -r/--rounds: %s", optarg);
                    MPI_Finalize();
                    exit(EINVAL);
                }
                break;
            case 'm':
                gMethod = bounce_method_parse(optarg);
                if ( gMethod == bounce_method_max ) {
                    ERROR("invalid bounce method provided to -m/--method: %s", optarg);
                    MPI_Finalize();
                    exit(EINVAL);
                }
                DEBUG("method %s selected", bounce_method_names[gMethod]);
                break;
            case 'o':
                out_filename = optarg;
                DEBUG("will output to file %s", optarg);
                break;
            case 'R':
                dummy_ld = strtol(optarg, &endptr, 0);
                if ( endptr && (endptr > optarg) ) {
                    if ( (dummy_ld < 0) || (dummy_ld > INT_MAX) ) {
                        ERROR("invalid rank index (out of range) provided to -R/--root-rank: %ld", dummy_ld);
                        MPI_Finalize();
                        exit(EINVAL);
                    }
                    gRootRank = dummy_ld % size;
                    INFO("root rank %lld reduces to %d", dummy_ld, gRootRank);
                } else {
                    ERROR("invalid round count provided to -r/--rounds: %s", optarg);
                    MPI_Finalize();
                    exit(EINVAL);
                }
                break;
        }
    }
    
    INFO("MPI startup complete for rank %d of %d", rank, size);
    gMyRank = rank;
    
    DEBUG("registering SIGUSR2 handler in rank %d of %d", rank, size);
    sigaction(SIGUSR2, &newSIGUSR2, NULL);
    
    //
    // Get the primary rank setup to output progress...
    //
    if ( rank == gRootRank ) {
        if ( out_filename ) {
            out_fptr = fopen(out_filename, "w");
            if ( ! out_fptr ) {
                ERROR("failed to open output file `%s` (errno = %d)", out_filename, errno);
                MPI_Finalize();
                exit(errno);
            }
        } else {
            out_fptr = stdout;
        }
        INFO("primary rank output file opened for writing");
    }
    
    INFO("initialization complete for rank %d of %d", rank, size);
    
    DEBUG("allocating the ball in rank %d of %d", rank, size);
    the_ball = malloc(gSize);
    if ( ! the_ball ) {
        ERROR("failed to allocate ball of %llu bytes (errno = %d) in rank %d of %d", gSize, errno, rank, size);
        if ( (rank == gRootRank) && (out_filename) ) {
            fclose(out_fptr);
            INFO("primary rank output file closed in rank %d of %d", rank, size);
        }
        MPI_Finalize();
        exit(errno);
    }
    INFO("ball allocated in rank %d of %d", rank, size);

    //
    // Wait for everyone to catch up:
    //
    MPI_Barrier(MPI_COMM_WORLD);
    INFO("MPI barrier reached for rank %d of %d", rank, size);
    
    //
    // Which method?
    //
    if ( gRounds != 0 ) {
        switch ( gMethod ) {
    
            case bounce_method_sendrecv: {
                if ( rank == gRootRank ) {
                    do {
                        // Start the game -- pass the ball to the next worker THEN wait for a response
                        // from the previous worker:
                        if ( gEarlyTermination ) break;
                        if ( (gRounds >= 0) && (round >= gRounds) ) break;
                        fprintf(out_fptr, "Started round %lld\n", round);fflush(out_fptr);
                        MPI_Send(the_ball, gSize, MPI_BYTE, (rank + 1) % size, 0, MPI_COMM_WORLD);
                        INFO("[*] Ball sent from %d to %d", rank, (rank + 1) % size);
                        if ( gEarlyTermination ) break;
                        MPI_Recv(the_ball, gSize, MPI_BYTE, (rank + size - 1) % size, 0, MPI_COMM_WORLD, &status);
                        INFO("[*] Ball received from %d to %d", (rank + size - 1) % size, rank);
                        round++;
                    } while ( 1 );
                    if ( gEarlyTermination ) {
                        fprintf(out_fptr, "Early termination on signal %d at round %lld in rank %d of %d\n", gEarlyTermination, round, rank, size); fflush(out_fptr);
                    }
                } else {
                    do {
                        // Receive the ball from the previous worker THEN pass it to the next:
                        if ( gEarlyTermination ) break;
                        if ( (gRounds >= 0) && (round >= gRounds) ) break;
                        MPI_Recv(the_ball, gSize, MPI_BYTE, (rank + size - 1) % size, 0, MPI_COMM_WORLD, &status);
                        INFO("[ ] Ball received from %d to %d", (rank + size - 1) % size, rank);
                        if ( gEarlyTermination ) break;
                        MPI_Send(the_ball, gSize, MPI_BYTE, (rank + 1) % size, 0, MPI_COMM_WORLD);
                        INFO("[ ] Ball sent from %d to %d", rank, (rank + 1) % size);
                        round++;
                    } while ( 1 );
                    if ( gEarlyTermination ) {
                        INFO("Early termination on signal %d at round %lld in rank %d of %d", gEarlyTermination, round, rank, size);
                    }
                }
                break;
            }
            
            case bounce_method_broadcast: {
                int         root_rank = gRootRank;
                
                while ( 1 ) {
                    if ( gEarlyTermination ) break;
                    if ( (gRounds >= 0) && (round >= gRounds) ) break;
                    if ( (root_rank == gRootRank) && (rank == gRootRank) ) {
                        fprintf(out_fptr, "Started round %lld\n", round); fflush(out_fptr);
                    }
                    if ( rank == root_rank ) {
                        INFO("[*] Ball sent from %d", rank);
                    }
                    MPI_Bcast(the_ball, gSize, MPI_BYTE, root_rank, MPI_COMM_WORLD);
                    if ( rank != root_rank ) {
                        INFO("[ ] Ball received in %d", rank);
                    }
                    root_rank = (root_rank + 1) % size;
                    if ( root_rank == gRootRank ) {
                        round++;
                    }
                }
                if ( gEarlyTermination ) {
                    if ( rank == gRootRank ) {
                        fprintf(out_fptr, "Early termination on signal %d at round %lld in rank %d of %d\n", gEarlyTermination, round, rank, size); fflush(out_fptr);
                    } else {
                        INFO("Early termination on signal %d at round %lld in rank %d of %d", gEarlyTermination, round, rank, size);
                    }
                }
            }
        
        }
        INFO("ball-passing loop has exited in rank %d of %d", rank, size);
    }
    
    free(the_ball);
    INFO("ball deallocated in rank %d of %d", rank, size);

    if ( (rank == gRootRank) && (out_filename) ) {
        fclose(out_fptr);
        INFO("primary rank output file closed in rank %d of %d", rank, size);
    }
    
    MPI_Barrier(MPI_COMM_WORLD);
    INFO("MPI barrier reached for rank %d of %d", rank, size);
    
    MPI_Finalize();
    INFO("MPI_Finalize() called, exiting now from rank %d of %d", rank, size);

    return 0;
}

