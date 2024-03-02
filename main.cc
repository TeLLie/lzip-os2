/* Lzip - LZMA lossless data compressor
   Copyright (C) 2008-2024 Antonio Diaz Diaz.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
   Exit status: 0 for a normal exit, 1 for environmental problems
   (file not found, invalid command-line options, I/O errors, etc), 2 to
   indicate a corrupt or invalid input file, 3 for an internal consistency
   error (e.g., bug) which caused lzip to panic.
*/

#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>		// CHAR_BIT, SSIZE_MAX
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include <fcntl.h>
#include <stdint.h>		// SIZE_MAX
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
#if defined __MSVCRT__ || defined __OS2__ || defined __DJGPP__
#include <io.h>
#if defined __MSVCRT__
#include <direct.h>
#define fchmod(x,y) 0
#define fchown(x,y,z) 0
#define mkdir(name,mode) _mkdir(name)
#define strtoull std::strtoul
#define SIGHUP SIGTERM
#define S_ISSOCK(x) 0
#ifndef S_IRGRP
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IROTH 0
#define S_IWOTH 0
#endif
#endif
#if defined __DJGPP__
#define S_ISSOCK(x) 0
#define S_ISVTX 0
#endif
#endif

#include "arg_parser.h"
#include "lzip.h"
#include "decoder.h"
#include "encoder_base.h"
#include "encoder.h"
#include "fast_encoder.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#if CHAR_BIT != 8
#error "Environments where CHAR_BIT != 8 are not supported."
#endif

#if ( defined  SIZE_MAX &&  SIZE_MAX < UINT_MAX ) || \
    ( defined SSIZE_MAX && SSIZE_MAX <  INT_MAX )
#error "Environments where 'size_t' is narrower than 'int' are not supported."
#endif

int verbosity = 0;

namespace {

const char * const program_name = "lzip";
const char * const program_year = "2024";
const char * invocation_name = program_name;		// default value

const struct { const char * from; const char * to; } known_extensions[] = {
  { ".lz",  ""     },
  { ".tlz", ".tar" },
  { 0,      0      } };

struct Lzma_options
  {
  int dictionary_size;		// 4 KiB .. 512 MiB
  int match_len_limit;		// 5 .. 273
  };

enum Mode { m_compress, m_decompress, m_list, m_test };

/* Variables used in signal handler context.
   They are not declared volatile because the handler never returns. */
std::string output_filename;
int outfd = -1;
bool delete_output_on_interrupt = false;


void show_help()
  {
  std::printf( "Lzip is a lossless data compressor with a user interface similar to the one\n"
               "of gzip or bzip2. Lzip uses a simplified form of the 'Lempel-Ziv-Markov\n"
               "chain-Algorithm' (LZMA) stream format to maximize interoperability. The\n"
               "maximum dictionary size is 512 MiB so that any lzip file can be decompressed\n"
               "on 32-bit machines. Lzip provides accurate and robust 3-factor integrity\n"
               "checking. Lzip can compress about as fast as gzip (lzip -0) or compress most\n"
               "files more than bzip2 (lzip -9). Decompression speed is intermediate between\n"
               "gzip and bzip2. Lzip is better than gzip and bzip2 from a data recovery\n"
               "perspective. Lzip has been designed, written, and tested with great care to\n"
               "replace gzip and bzip2 as the standard general-purpose compressed format for\n"
               "Unix-like systems.\n"
               "\nUsage: %s [options] [files]\n", invocation_name );
  std::printf( "\nOptions:\n"
               "  -h, --help                     display this help and exit\n"
               "  -V, --version                  output version information and exit\n"
               "  -a, --trailing-error           exit with error status if trailing data\n"
               "  -b, --member-size=<bytes>      set member size limit in bytes\n"
               "  -c, --stdout                   write to standard output, keep input files\n"
               "  -d, --decompress               decompress, test compressed file integrity\n"
               "  -f, --force                    overwrite existing output files\n"
               "  -F, --recompress               force re-compression of compressed files\n"
               "  -k, --keep                     keep (don't delete) input files\n"
               "  -l, --list                     print (un)compressed file sizes\n"
               "  -m, --match-length=<bytes>     set match length limit in bytes [36]\n"
               "  -o, --output=<file>            write to <file>, keep input files\n"
               "  -q, --quiet                    suppress all messages\n"
               "  -s, --dictionary-size=<bytes>  set dictionary size limit in bytes [8 MiB]\n"
               "  -S, --volume-size=<bytes>      set volume size limit in bytes\n"
               "  -t, --test                     test compressed file integrity\n"
               "  -v, --verbose                  be verbose (a 2nd -v gives more)\n"
               "  -0 .. -9                       set compression level [default 6]\n"
               "      --fast                     alias for -0\n"
               "      --best                     alias for -9\n"
               "      --empty-error              exit with error status if empty member in file\n"
               "      --marking-error            exit with error status if 1st LZMA byte not 0\n"
               "      --loose-trailing           allow trailing data seeming corrupt header\n"
               "\nIf no file names are given, or if a file is '-', lzip compresses or\n"
               "decompresses from standard input to standard output.\n"
               "Numbers may be followed by a multiplier: k = kB = 10^3 = 1000,\n"
               "Ki = KiB = 2^10 = 1024, M = 10^6, Mi = 2^20, G = 10^9, Gi = 2^30, etc...\n"
               "Dictionary sizes 12 to 29 are interpreted as powers of two, meaning 2^12 to\n"
               "2^29 bytes.\n"
               "\nThe bidimensional parameter space of LZMA can't be mapped to a linear scale\n"
               "optimal for all files. If your files are large, very repetitive, etc, you\n"
               "may need to use the options --dictionary-size and --match-length directly\n"
               "to achieve optimal performance.\n"
               "\nTo extract all the files from archive 'foo.tar.lz', use the commands\n"
               "'tar -xf foo.tar.lz' or 'lzip -cd foo.tar.lz | tar -xf -'.\n"
               "\nExit status: 0 for a normal exit, 1 for environmental problems\n"
               "(file not found, invalid command-line options, I/O errors, etc), 2 to\n"
               "indicate a corrupt or invalid input file, 3 for an internal consistency\n"
               "error (e.g., bug) which caused lzip to panic.\n"
               "\nThe ideas embodied in lzip are due to (at least) the following people:\n"
               "Abraham Lempel and Jacob Ziv (for the LZ algorithm), Andrei Markov (for the\n"
               "definition of Markov chains), G.N.N. Martin (for the definition of range\n"
               "encoding), Igor Pavlov (for putting all the above together in LZMA), and\n"
               "Julian Seward (for bzip2's CLI).\n"
               "\nReport bugs to lzip-bug@nongnu.org\n"
               "Lzip home page: http://www.nongnu.org/lzip/lzip.html\n" );
  }


void show_version()
  {
  std::printf( "%s %s\n", program_name, PROGVERSION );
  std::printf( "Copyright (C) %s Antonio Diaz Diaz.\n", program_year );
  std::printf( "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n"
               "This is free software: you are free to change and redistribute it.\n"
               "There is NO WARRANTY, to the extent permitted by law.\n" );
  }

} // end namespace

void Pretty_print::operator()( const char * const msg ) const
  {
  if( verbosity < 0 ) return;
  if( first_post )
    {
    first_post = false;
    std::fputs( padded_name.c_str(), stderr );
    if( !msg ) std::fflush( stderr );
    }
  if( msg ) std::fprintf( stderr, "%s\n", msg );
  }


const char * bad_version( const unsigned version )
  {
  static char buf[80];
  snprintf( buf, sizeof buf, "Version %u member format not supported.",
            version );
  return buf;
  }


const char * format_ds( const unsigned dictionary_size )
  {
  enum { bufsize = 16, factor = 1024, n = 3 };
  static char buf[bufsize];
  const char * const prefix[n] = { "Ki", "Mi", "Gi" };
  const char * p = "";
  const char * np = "  ";
  unsigned num = dictionary_size;
  bool exact = ( num % factor == 0 );

  for( int i = 0; i < n && ( num > 9999 || ( exact && num >= factor ) ); ++i )
    { num /= factor; if( num % factor != 0 ) exact = false;
      p = prefix[i]; np = ""; }
  snprintf( buf, bufsize, "%s%4u %sB", np, num, p );
  return buf;
  }


void show_header( const unsigned dictionary_size )
  {
  std::fprintf( stderr, "dict %s, ", format_ds( dictionary_size ) );
  }

namespace {

// separate numbers of 5 or more digits in groups of 3 digits using '_'
const char * format_num3( unsigned long long num )
  {
  enum { buffers = 8, bufsize = 4 * sizeof num, n = 10 };
  const char * const si_prefix = "kMGTPEZYRQ";
  const char * const binary_prefix = "KMGTPEZYRQ";
  static char buffer[buffers][bufsize];	// circle of static buffers for printf
  static int current = 0;

  char * const buf = buffer[current++]; current %= buffers;
  char * p = buf + bufsize - 1;		// fill the buffer backwards
  *p = 0;	// terminator
  if( num > 1024 )
    {
    char prefix = 0;			// try binary first, then si
    for( int i = 0; i < n && num != 0 && num % 1024 == 0; ++i )
      { num /= 1024; prefix = binary_prefix[i]; }
    if( prefix ) *(--p) = 'i';
    else
      for( int i = 0; i < n && num != 0 && num % 1000 == 0; ++i )
        { num /= 1000; prefix = si_prefix[i]; }
    if( prefix ) *(--p) = prefix;
    }
  const bool split = num >= 10000;

  for( int i = 0; ; )
    {
    *(--p) = num % 10 + '0'; num /= 10; if( num == 0 ) break;
    if( split && ++i >= 3 ) { i = 0; *(--p) = '_'; }
    }
  return p;
  }


void show_option_error( const char * const arg, const char * const msg,
                        const char * const option_name )
  {
  if( verbosity >= 0 )
    std::fprintf( stderr, "%s: '%s': %s option '%s'.\n",
                  program_name, arg, msg, option_name );
  }


// Recognized formats: <num>k, <num>Ki, <num>[MGTPEZYRQ][i]
unsigned long long getnum( const char * const arg,
                           const char * const option_name,
                           const unsigned long long llimit,
                           const unsigned long long ulimit )
  {
  char * tail;
  errno = 0;
  unsigned long long result = strtoull( arg, &tail, 0 );
  if( tail == arg )
    { show_option_error( arg, "Bad or missing numerical argument in",
                         option_name ); std::exit( 1 ); }

  if( !errno && tail[0] )
    {
    const unsigned factor = ( tail[1] == 'i' ) ? 1024 : 1000;
    int exponent = 0;				// 0 = bad multiplier
    switch( tail[0] )
      {
      case 'Q': exponent = 10; break;
      case 'R': exponent = 9; break;
      case 'Y': exponent = 8; break;
      case 'Z': exponent = 7; break;
      case 'E': exponent = 6; break;
      case 'P': exponent = 5; break;
      case 'T': exponent = 4; break;
      case 'G': exponent = 3; break;
      case 'M': exponent = 2; break;
      case 'K': if( factor == 1024 ) exponent = 1; break;
      case 'k': if( factor == 1000 ) exponent = 1; break;
      }
    if( exponent <= 0 )
      { show_option_error( arg, "Bad multiplier in numerical argument of",
                           option_name ); std::exit( 1 ); }
    for( int i = 0; i < exponent; ++i )
      {
      if( ulimit / factor >= result ) result *= factor;
      else { errno = ERANGE; break; }
      }
    }
  if( !errno && ( result < llimit || result > ulimit ) ) errno = ERANGE;
  if( errno )
    {
    if( verbosity >= 0 )
      std::fprintf( stderr, "%s: '%s': Value out of limits [%s,%s] in "
                    "option '%s'.\n", program_name, arg, format_num3( llimit ),
                    format_num3( ulimit ), option_name );
    std::exit( 1 );
    }
  return result;
  }


int get_dict_size( const char * const arg, const char * const option_name )
  {
  char * tail;
  const long bits = std::strtol( arg, &tail, 0 );
  if( bits >= min_dictionary_bits &&
      bits <= max_dictionary_bits && *tail == 0 )
    return 1 << bits;
  return getnum( arg, option_name, min_dictionary_size, max_dictionary_size );
  }


void set_mode( Mode & program_mode, const Mode new_mode )
  {
  if( program_mode != m_compress && program_mode != new_mode )
    {
    show_error( "Only one operation can be specified.", 0, true );
    std::exit( 1 );
    }
  program_mode = new_mode;
  }


int extension_index( const std::string & name )
  {
  for( int eindex = 0; known_extensions[eindex].from; ++eindex )
    {
    const std::string ext( known_extensions[eindex].from );
    if( name.size() > ext.size() &&
        name.compare( name.size() - ext.size(), ext.size(), ext ) == 0 )
      return eindex;
    }
  return -1;
  }


void set_c_outname( const std::string & name, const bool filenames_given,
                    const bool force_ext, const bool multifile )
  {
  /* zupdate < 1.9 depends on lzip adding the extension '.lz' to name when
     reading from standard input. */
  output_filename = name;
  if( multifile ) output_filename += "00001";
  if( force_ext || multifile ||
      ( !filenames_given && extension_index( output_filename ) < 0 ) )
    output_filename += known_extensions[0].from;
  }


void set_d_outname( const std::string & name, const int eindex )
  {
  if( eindex >= 0 )
    {
    const std::string from( known_extensions[eindex].from );
    if( name.size() > from.size() )
      {
      output_filename.assign( name, 0, name.size() - from.size() );
      output_filename += known_extensions[eindex].to;
      return;
      }
    }
  output_filename = name; output_filename += ".out";
  if( verbosity >= 1 )
    std::fprintf( stderr, "%s: %s: Can't guess original name -- using '%s'\n",
                  program_name, name.c_str(), output_filename.c_str() );
  }

} // end namespace

int open_instream( const char * const name, struct stat * const in_statsp,
                   const bool one_to_one, const bool reg_only )
  {
  int infd = open( name, O_RDONLY | O_BINARY );
  if( infd < 0 )
    show_file_error( name, "Can't open input file", errno );
  else
    {
    const int i = fstat( infd, in_statsp );
    const mode_t mode = in_statsp->st_mode;
    const bool can_read = ( i == 0 && !reg_only &&
                            ( S_ISBLK( mode ) || S_ISCHR( mode ) ||
                              S_ISFIFO( mode ) || S_ISSOCK( mode ) ) );
    if( i != 0 || ( !S_ISREG( mode ) && ( !can_read || one_to_one ) ) )
      {
      if( verbosity >= 0 )
        std::fprintf( stderr, "%s: %s: Input file is not a regular file%s.\n",
                      program_name, name, ( can_read && one_to_one ) ?
                      ",\n  and neither '-c' nor '-o' were specified" : "" );
      close( infd );
      infd = -1;
      }
    }
  return infd;
  }

namespace {

int open_instream2( const char * const name, struct stat * const in_statsp,
                    const Mode program_mode, const int eindex,
                    const bool one_to_one, const bool recompress )
  {
  if( program_mode == m_compress && !recompress && eindex >= 0 )
    {
    if( verbosity >= 0 )
      std::fprintf( stderr, "%s: %s: Input file already has '%s' suffix.\n",
                    program_name, name, known_extensions[eindex].from );
    return -1;
    }
  return open_instream( name, in_statsp, one_to_one, false );
  }


bool make_dirs( const std::string & name )
  {
  int i = name.size();
  while( i > 0 && name[i-1] != '/' ) --i;	// remove last component
  while( i > 0 && name[i-1] == '/' ) --i;	// remove slash(es)
  const int dirsize = i;	// size of dirname without trailing slash(es)

  for( i = 0; i < dirsize; )	// if dirsize == 0, dirname is '/' or empty
    {
    while( i < dirsize && name[i] == '/' ) ++i;
    const int first = i;
    while( i < dirsize && name[i] != '/' ) ++i;
    if( first < i )
      {
      const std::string partial( name, 0, i );
      const mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
      struct stat st;
      if( stat( partial.c_str(), &st ) == 0 )
        { if( !S_ISDIR( st.st_mode ) ) { errno = ENOTDIR; return false; } }
      else if( mkdir( partial.c_str(), mode ) != 0 && errno != EEXIST )
        return false;		// if EEXIST, another process created the dir
      }
    }
  return true;
  }


bool open_outstream( const bool force, const bool protect )
  {
  const mode_t usr_rw = S_IRUSR | S_IWUSR;
  const mode_t all_rw = usr_rw | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  const mode_t outfd_mode = protect ? usr_rw : all_rw;
  int flags = O_CREAT | O_WRONLY | O_BINARY;
  if( force ) flags |= O_TRUNC; else flags |= O_EXCL;

  outfd = -1;
  if( output_filename.size() &&
      output_filename[output_filename.size()-1] == '/' ) errno = EISDIR;
  else {
    if( !protect && !make_dirs( output_filename ) )
      { show_file_error( output_filename.c_str(),
          "Error creating intermediate directory", errno ); return false; }
    outfd = open( output_filename.c_str(), flags, outfd_mode );
    if( outfd >= 0 ) { delete_output_on_interrupt = true; return true; }
    if( errno == EEXIST )
      { show_file_error( output_filename.c_str(),
          "Output file already exists, skipping." ); return false; }
    }
  show_file_error( output_filename.c_str(), "Can't create output file", errno );
  return false;
  }


void set_signals( void (*action)(int) )
  {
  std::signal( SIGHUP, action );
  std::signal( SIGINT, action );
  std::signal( SIGTERM, action );
  }


void cleanup_and_fail( const int retval )
  {
  set_signals( SIG_IGN );			// ignore signals
  if( delete_output_on_interrupt )
    {
    delete_output_on_interrupt = false;
    show_file_error( output_filename.c_str(),
                     "Deleting output file, if it exists." );
    if( outfd >= 0 ) { close( outfd ); outfd = -1; }
    if( std::remove( output_filename.c_str() ) != 0 && errno != ENOENT )
      show_error( "warning: deletion of output file failed", errno );
    }
  std::exit( retval );
  }


extern "C" void signal_handler( int )
  {
  show_error( "Control-C or similar caught, quitting." );
  cleanup_and_fail( 1 );
  }


bool check_tty_in( const char * const input_filename, const int infd,
                   const Mode program_mode, int & retval )
  {
  if( ( program_mode == m_decompress || program_mode == m_test ) &&
      isatty( infd ) )				// for example /dev/tty
    { show_file_error( input_filename,
                       "I won't read compressed data from a terminal." );
      close( infd ); set_retval( retval, 2 );
      if( program_mode != m_test ) cleanup_and_fail( retval );
      return false; }
  return true;
  }

bool check_tty_out( const Mode program_mode )
  {
  if( program_mode == m_compress && isatty( outfd ) )
    { show_file_error( output_filename.size() ?
                       output_filename.c_str() : "(stdout)",
                       "I won't write compressed data to a terminal." );
      return false; }
  return true;
  }


// Set permissions, owner, and times.
void close_and_set_permissions( const struct stat * const in_statsp )
  {
  bool warning = false;
  if( in_statsp )
    {
    const mode_t mode = in_statsp->st_mode;
    // fchown in many cases returns with EPERM, which can be safely ignored.
    if( fchown( outfd, in_statsp->st_uid, in_statsp->st_gid ) == 0 )
      { if( fchmod( outfd, mode ) != 0 ) warning = true; }
    else
      if( errno != EPERM ||
          fchmod( outfd, mode & ~( S_ISUID | S_ISGID | S_ISVTX ) ) != 0 )
        warning = true;
    }
  if( close( outfd ) != 0 )
    { show_file_error( output_filename.c_str(), "Error closing output file",
                       errno ); cleanup_and_fail( 1 ); }
  outfd = -1;
  delete_output_on_interrupt = false;
  if( in_statsp )
    {
    struct utimbuf t;
    t.actime = in_statsp->st_atime;
    t.modtime = in_statsp->st_mtime;
    if( utime( output_filename.c_str(), &t ) != 0 ) warning = true;
    }
  if( warning && verbosity >= 1 )
    show_file_error( output_filename.c_str(),
                     "warning: can't change output file attributes", errno );
  }


bool next_filename()
  {
  const unsigned name_len = output_filename.size();
  const unsigned ext_len = std::strlen( known_extensions[0].from );
  if( name_len >= ext_len + 5 )				// "*00001.lz"
    for( int i = name_len - ext_len - 1, j = 0; j < 5; --i, ++j )
      {
      if( output_filename[i] < '9' ) { ++output_filename[i]; return true; }
      else output_filename[i] = '0';
      }
  return false;
  }


int compress( const unsigned long long cfile_size,
              const unsigned long long member_size,
              const unsigned long long volume_size, const int infd,
              const Lzma_options & encoder_options, const Pretty_print & pp,
              const struct stat * const in_statsp, const bool zero )
  {
  int retval = 0;
  LZ_encoder_base * encoder = 0;		// polymorphic encoder
  if( verbosity >= 1 ) pp();

  if( zero )
    encoder = new FLZ_encoder( infd, outfd );
  else
    {
    Lzip_header header;
    if( header.dictionary_size( encoder_options.dictionary_size ) &&
        encoder_options.match_len_limit >= min_match_len_limit &&
        encoder_options.match_len_limit <= max_match_len )
      encoder = new LZ_encoder( header.dictionary_size(),
                                encoder_options.match_len_limit, infd, outfd );
    else internal_error( "invalid argument to encoder." );
    }

  unsigned long long in_size = 0, out_size = 0, partial_volume_size = 0;
  while( true )		// encode one member per iteration
    {
    const unsigned long long size = ( volume_size > 0 ) ?
      std::min( member_size, volume_size - partial_volume_size ) : member_size;
    show_cprogress( cfile_size, in_size, encoder, &pp );	// init
    if( !encoder->encode_member( size ) )
      { pp( "Encoder error." ); retval = 1; break; }
    in_size += encoder->data_position();
    out_size += encoder->member_position();
    if( encoder->data_finished() ) break;
    if( volume_size > 0 )
      {
      partial_volume_size += encoder->member_position();
      if( partial_volume_size >= volume_size - min_dictionary_size )
        {
        partial_volume_size = 0;
        if( delete_output_on_interrupt )
          {
          close_and_set_permissions( in_statsp );
          if( !next_filename() )
            { pp( "Too many volume files." ); retval = 1; break; }
          if( !open_outstream( true, in_statsp ) ) { retval = 1; break; }
          }
        }
      }
    encoder->reset();
    }

  if( retval == 0 && verbosity >= 1 )
    {
    if( in_size == 0 || out_size == 0 )
      std::fputs( " no data compressed.\n", stderr );
    else
      std::fprintf( stderr, "%6.3f:1, %5.2f%% ratio, %5.2f%% saved, "
                            "%llu in, %llu out.\n",
                    (double)in_size / out_size,
                    ( 100.0 * out_size ) / in_size,
                    100.0 - ( ( 100.0 * out_size ) / in_size ),
                    in_size, out_size );
    }
  delete encoder;
  return retval;
  }


unsigned char xdigit( const unsigned value )	// hex digit for 'value'
  {
  if( value <= 9 ) return '0' + value;
  if( value <= 15 ) return 'A' + value - 10;
  return 0;
  }


bool show_trailing_data( const uint8_t * const data, const int size,
                         const Pretty_print & pp, const bool all,
                         const int ignore_trailing )	// -1 = show
  {
  if( verbosity >= 4 || ignore_trailing <= 0 )
    {
    std::string msg;
    if( !all ) msg = "first bytes of ";
    msg += "trailing data = ";
    for( int i = 0; i < size; ++i )
      {
      msg += xdigit( data[i] >> 4 );
      msg += xdigit( data[i] & 0x0F );
      msg += ' ';
      }
    msg += '\'';
    for( int i = 0; i < size; ++i )
      { if( std::isprint( data[i] ) ) msg += data[i]; else msg += '.'; }
    msg += '\'';
    pp( msg.c_str() );
    if( ignore_trailing == 0 ) show_file_error( pp.name(), trailing_msg );
    }
  return ignore_trailing > 0;
  }


int decompress( const unsigned long long cfile_size, const int infd,
                const Cl_options & cl_opts, const Pretty_print & pp,
                const bool testing )
  {
  unsigned long long partial_file_pos = 0;
  Range_decoder rdec( infd );
  int retval = 0;

  for( bool first_member = true; ; first_member = false )
    {
    Lzip_header header;
    rdec.reset_member_position();
    const int size = rdec.read_data( header.data, header.size );
    if( rdec.finished() )			// End Of File
      {
      if( first_member )
        { show_file_error( pp.name(), "File ends unexpectedly at member header." );
          retval = 2; }
      else if( header.check_prefix( size ) )
        { pp( "Truncated header in multimember file." );
          show_trailing_data( header.data, size, pp, true, -1 ); retval = 2; }
      else if( size > 0 && !show_trailing_data( header.data, size, pp, true,
                                 cl_opts.ignore_trailing ) ) retval = 2;
      break;
      }
    if( !header.check_magic() )
      {
      if( first_member )
        { show_file_error( pp.name(), bad_magic_msg ); retval = 2; }
      else if( !cl_opts.loose_trailing && header.check_corrupt() )
        { pp( corrupt_mm_msg );
          show_trailing_data( header.data, size, pp, false, -1 ); retval = 2; }
      else if( !show_trailing_data( header.data, size, pp, false,
                                    cl_opts.ignore_trailing ) ) retval = 2;
      break;
      }
    if( !header.check_version() )
      { pp( bad_version( header.version() ) ); retval = 2; break; }
    const unsigned dictionary_size = header.dictionary_size();
    if( !isvalid_ds( dictionary_size ) )
      { pp( bad_dict_msg ); retval = 2; break; }

    if( verbosity >= 2 || ( verbosity == 1 && first_member ) ) pp();

    LZ_decoder decoder( rdec, dictionary_size, outfd );
    show_dprogress( cfile_size, partial_file_pos, &rdec, &pp );	// init
    const int result = decoder.decode_member( cl_opts, pp );
    partial_file_pos += rdec.member_position();
    if( result != 0 )
      {
      if( verbosity >= 0 && result <= 2 )
        {
        pp();
        std::fprintf( stderr, "%s at pos %llu\n", ( result == 2 ) ?
                      "File ends unexpectedly" : "Decoder error",
                      partial_file_pos );
        }
      else if( result == 5 ) pp( empty_msg );
      else if( result == 6 ) pp( marking_msg );
      retval = 2; break;
      }
    if( verbosity >= 2 )
      { std::fputs( testing ? "ok\n" : "done\n", stderr ); pp.reset(); }
    }
  if( verbosity == 1 && retval == 0 )
    std::fputs( testing ? "ok\n" : "done\n", stderr );
  return retval;
  }

} // end namespace


void show_error( const char * const msg, const int errcode, const bool help )
  {
  if( verbosity < 0 ) return;
  if( msg && msg[0] )
    std::fprintf( stderr, "%s: %s%s%s\n", program_name, msg,
                  ( errcode > 0 ) ? ": " : "",
                  ( errcode > 0 ) ? std::strerror( errcode ) : "" );
  if( help )
    std::fprintf( stderr, "Try '%s --help' for more information.\n",
                  invocation_name );
  }


void show_file_error( const char * const filename, const char * const msg,
                      const int errcode )
  {
  if( verbosity >= 0 )
    std::fprintf( stderr, "%s: %s: %s%s%s\n", program_name, filename, msg,
                  ( errcode > 0 ) ? ": " : "",
                  ( errcode > 0 ) ? std::strerror( errcode ) : "" );
  }


void internal_error( const char * const msg )
  {
  if( verbosity >= 0 )
    std::fprintf( stderr, "%s: internal error: %s\n", program_name, msg );
  std::exit( 3 );
  }


void show_cprogress( const unsigned long long cfile_size,
                     const unsigned long long partial_size,
                     const Matchfinder_base * const m,
                     const Pretty_print * const p )
  {
  static unsigned long long csize = 0;		// file_size / 100
  static unsigned long long psize = 0;
  static const Matchfinder_base * mb = 0;
  static const Pretty_print * pp = 0;
  static bool enabled = true;

  if( !enabled ) return;
  if( p )					// initialize static vars
    {
    if( verbosity < 2 || !isatty( STDERR_FILENO ) ) { enabled = false; return; }
    csize = cfile_size; psize = partial_size; mb = m; pp = p;
    }
  if( mb && pp )
    {
    const unsigned long long pos = psize + mb->data_position();
    if( csize > 0 )
      std::fprintf( stderr, "%4llu%%  %.1f MB\r", pos / csize, pos / 1000000.0 );
    else
      std::fprintf( stderr, "  %.1f MB\r", pos / 1000000.0 );
    pp->reset(); (*pp)();			// restore cursor position
    }
  }


void show_dprogress( const unsigned long long cfile_size,
                     const unsigned long long partial_size,
                     const Range_decoder * const d,
                     const Pretty_print * const p )
  {
  static unsigned long long csize = 0;		// file_size / 100
  static unsigned long long psize = 0;
  static const Range_decoder * rdec = 0;
  static const Pretty_print * pp = 0;
  static int counter = 0;
  static bool enabled = true;

  if( !enabled ) return;
  if( p )					// initialize static vars
    {
    if( verbosity < 2 || !isatty( STDERR_FILENO ) ) { enabled = false; return; }
    csize = cfile_size; psize = partial_size; rdec = d; pp = p; counter = 0;
    }
  if( rdec && pp && --counter <= 0 )
    {
    const unsigned long long pos = psize + rdec->member_position();
    counter = 7;		// update display every 114688 bytes
    if( csize > 0 )
      std::fprintf( stderr, "%4llu%%  %.1f MB\r", pos / csize, pos / 1000000.0 );
    else
      std::fprintf( stderr, "  %.1f MB\r", pos / 1000000.0 );
    pp->reset(); (*pp)();			// restore cursor position
    }
  }


int main( const int argc, const char * const argv[] )
  {
  /* Mapping from gzip/bzip2 style 0..9 compression levels to the
     corresponding LZMA compression parameters. */
  const Lzma_options option_mapping[] =
    {
    { 1 << 16,  16 },		// -0
    { 1 << 20,   5 },		// -1
    { 3 << 19,   6 },		// -2
    { 1 << 21,   8 },		// -3
    { 3 << 20,  12 },		// -4
    { 1 << 22,  20 },		// -5
    { 1 << 23,  36 },		// -6
    { 1 << 24,  68 },		// -7
    { 3 << 23, 132 },		// -8
    { 1 << 25, 273 } };		// -9
  Lzma_options encoder_options = option_mapping[6];	// default = "-6"
  const unsigned long long max_member_size = 0x0008000000000000ULL; // 2 PiB
  const unsigned long long max_volume_size = 0x4000000000000000ULL; // 4 EiB
  unsigned long long member_size = max_member_size;
  unsigned long long volume_size = 0;
  std::string default_output_filename;
  Mode program_mode = m_compress;
  Cl_options cl_opts;		// command-line options
  bool force = false;
  bool keep_input_files = false;
  bool recompress = false;
  bool to_stdout = false;
  bool zero = false;
  if( argc > 0 ) invocation_name = argv[0];

  enum { opt_eer = 256, opt_lt, opt_mer };
  const Arg_parser::Option options[] =
    {
    { '0', "fast",               Arg_parser::no  },
    { '1', 0,                    Arg_parser::no  },
    { '2', 0,                    Arg_parser::no  },
    { '3', 0,                    Arg_parser::no  },
    { '4', 0,                    Arg_parser::no  },
    { '5', 0,                    Arg_parser::no  },
    { '6', 0,                    Arg_parser::no  },
    { '7', 0,                    Arg_parser::no  },
    { '8', 0,                    Arg_parser::no  },
    { '9', "best",               Arg_parser::no  },
    { 'a', "trailing-error",     Arg_parser::no  },
    { 'b', "member-size",        Arg_parser::yes },
    { 'c', "stdout",             Arg_parser::no  },
    { 'd', "decompress",         Arg_parser::no  },
    { 'f', "force",              Arg_parser::no  },
    { 'F', "recompress",         Arg_parser::no  },
    { 'h', "help",               Arg_parser::no  },
    { 'k', "keep",               Arg_parser::no  },
    { 'l', "list",               Arg_parser::no  },
    { 'm', "match-length",       Arg_parser::yes },
    { 'n', "threads",            Arg_parser::yes },
    { 'o', "output",             Arg_parser::yes },
    { 'q', "quiet",              Arg_parser::no  },
    { 's', "dictionary-size",    Arg_parser::yes },
    { 'S', "volume-size",        Arg_parser::yes },
    { 't', "test",               Arg_parser::no  },
    { 'v', "verbose",            Arg_parser::no  },
    { 'V', "version",            Arg_parser::no  },
    { opt_eer, "empty-error",    Arg_parser::no  },
    { opt_lt,  "loose-trailing", Arg_parser::no  },
    { opt_mer, "marking-error",  Arg_parser::no  },
    {  0, 0,                     Arg_parser::no  } };

  const Arg_parser parser( argc, argv, options );
  if( parser.error().size() )				// bad option
    { show_error( parser.error().c_str(), 0, true ); return 1; }

  int argind = 0;
  for( ; argind < parser.arguments(); ++argind )
    {
    const int code = parser.code( argind );
    if( !code ) break;					// no more options
    const char * const pn = parser.parsed_name( argind ).c_str();
    const std::string & sarg = parser.argument( argind );
    const char * const arg = sarg.c_str();
    switch( code )
      {
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
                zero = ( code == '0' );
                encoder_options = option_mapping[code-'0']; break;
      case 'a': cl_opts.ignore_trailing = false; break;
      case 'b': member_size = getnum( arg, pn, 100000, max_member_size ); break;
      case 'c': to_stdout = true; break;
      case 'd': set_mode( program_mode, m_decompress ); break;
      case 'f': force = true; break;
      case 'F': recompress = true; break;
      case 'h': show_help(); return 0;
      case 'k': keep_input_files = true; break;
      case 'l': set_mode( program_mode, m_list ); break;
      case 'm': encoder_options.match_len_limit =
                  getnum( arg, pn, min_match_len_limit, max_match_len );
                zero = false; break;
      case 'n': break;
      case 'o': if( sarg == "-" ) to_stdout = true;
                else { default_output_filename = sarg; } break;
      case 'q': verbosity = -1; break;
      case 's': encoder_options.dictionary_size = get_dict_size( arg, pn );
                zero = false; break;
      case 'S': volume_size = getnum( arg, pn, 100000, max_volume_size ); break;
      case 't': set_mode( program_mode, m_test ); break;
      case 'v': if( verbosity < 4 ) ++verbosity; break;
      case 'V': show_version(); return 0;
      case opt_eer: cl_opts.ignore_empty = false; break;
      case opt_lt:  cl_opts.loose_trailing = true; break;
      case opt_mer: cl_opts.ignore_marking = false; break;
      default: internal_error( "uncaught option." );
      }
    } // end process options

#if defined __MSVCRT__ || defined __OS2__ || defined __DJGPP__
  setmode( STDIN_FILENO, O_BINARY );
  setmode( STDOUT_FILENO, O_BINARY );
#endif

  std::vector< std::string > filenames;
  bool filenames_given = false;
  for( ; argind < parser.arguments(); ++argind )
    {
    filenames.push_back( parser.argument( argind ) );
    if( filenames.back() != "-" ) filenames_given = true;
    }
  if( filenames.empty() ) filenames.push_back("-");

  if( program_mode == m_list ) return list_files( filenames, cl_opts );

  if( program_mode == m_compress )
    {
    if( volume_size > 0 && !to_stdout && default_output_filename.size() &&
        filenames.size() > 1 )
      { show_error( "Only can compress one file when using '-o' and '-S'.",
                    0, true ); return 1; }
    dis_slots.init();
    prob_prices.init();
    }
  else volume_size = 0;
  if( program_mode == m_test ) to_stdout = false;	// apply overrides
  if( program_mode == m_test || to_stdout ) default_output_filename.clear();

  if( to_stdout && program_mode != m_test )	// check tty only once
    { outfd = STDOUT_FILENO; if( !check_tty_out( program_mode ) ) return 1; }
  else outfd = -1;

  const bool to_file = !to_stdout && program_mode != m_test &&
                       default_output_filename.size();
  if( !to_stdout && program_mode != m_test && ( filenames_given || to_file ) )
    set_signals( signal_handler );

  Pretty_print pp( filenames );

  int failed_tests = 0;
  int retval = 0;
  const bool one_to_one = !to_stdout && program_mode != m_test && !to_file;
  bool stdin_used = false;
  struct stat in_stats;
  for( unsigned i = 0; i < filenames.size(); ++i )
    {
    std::string input_filename;
    int infd;

    pp.set_name( filenames[i] );
    if( filenames[i] == "-" )
      {
      if( stdin_used ) continue; else stdin_used = true;
      infd = STDIN_FILENO;
      if( !check_tty_in( pp.name(), infd, program_mode, retval ) ) continue;
      if( one_to_one ) { outfd = STDOUT_FILENO; output_filename.clear(); }
      }
    else
      {
      const int eindex = extension_index( input_filename = filenames[i] );
      infd = open_instream2( input_filename.c_str(), &in_stats, program_mode,
                             eindex, one_to_one, recompress );
      if( infd < 0 ) { set_retval( retval, 1 ); continue; }
      if( !check_tty_in( pp.name(), infd, program_mode, retval ) ) continue;
      if( one_to_one )			// open outfd after checking infd
        {
        if( program_mode == m_compress )
          set_c_outname( input_filename, true, true, volume_size > 0 );
        else set_d_outname( input_filename, eindex );
        if( !open_outstream( force, true ) )
          { close( infd ); set_retval( retval, 1 ); continue; }
        }
      }

    if( one_to_one && !check_tty_out( program_mode ) )
      { set_retval( retval, 1 ); return retval; }	// don't delete a tty

    if( to_file && outfd < 0 )		// open outfd after checking infd
      {
      if( program_mode == m_compress ) set_c_outname( default_output_filename,
                                       filenames_given, false, volume_size > 0 );
      else output_filename = default_output_filename;
      if( !open_outstream( force, false ) || !check_tty_out( program_mode ) )
        return 1;	// check tty only once and don't try to delete a tty
      }

    const struct stat * const in_statsp =
      ( input_filename.size() && one_to_one ) ? &in_stats : 0;
    const unsigned long long cfile_size =
      ( input_filename.size() && S_ISREG( in_stats.st_mode ) ) ?
        ( in_stats.st_size + 99 ) / 100 : 0;
    int tmp;
    try {
      if( program_mode == m_compress )
        tmp = compress( cfile_size, member_size, volume_size, infd,
                        encoder_options, pp, in_statsp, zero );
      else
        tmp = decompress( cfile_size, infd, cl_opts, pp, program_mode == m_test );
      }
    catch( std::bad_alloc & )
      { pp( ( program_mode == m_compress ) ?
            "Not enough memory. Try a smaller dictionary size." :
            "Not enough memory." ); tmp = 1; }
    catch( Error & e ) { pp(); show_error( e.msg, errno ); tmp = 1; }
    if( close( infd ) != 0 )
      { show_file_error( pp.name(), "Error closing input file", errno );
        set_retval( tmp, 1 ); }
    set_retval( retval, tmp );
    if( tmp )
      { if( program_mode != m_test ) cleanup_and_fail( retval );
        else ++failed_tests; }

    if( delete_output_on_interrupt && one_to_one )
      close_and_set_permissions( in_statsp );
    if( input_filename.size() && !keep_input_files && one_to_one &&
        ( program_mode != m_compress || volume_size == 0 ) )
      std::remove( input_filename.c_str() );
    }
  if( delete_output_on_interrupt )					// -o
    close_and_set_permissions( ( retval == 0 && !stdin_used &&
      filenames_given && filenames.size() == 1 ) ? &in_stats : 0 );
  else if( outfd >= 0 && close( outfd ) != 0 )				// -c
    {
    show_error( "Error closing stdout", errno );
    set_retval( retval, 1 );
    }
  if( failed_tests > 0 && verbosity >= 1 && filenames.size() > 1 )
    std::fprintf( stderr, "%s: warning: %d %s failed the test.\n",
                  program_name, failed_tests,
                  ( failed_tests == 1 ) ? "file" : "files" );
  return retval;
  }
