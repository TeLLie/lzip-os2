/*  Lzip - LZMA lossless data compressor
    Copyright (C) 2008-2019 Antonio Diaz Diaz.

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

#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <stdint.h>
#include <unistd.h>

#include "lzip.h"
#include "lzip_index.h"


namespace {

int seek_read( const int fd, uint8_t * const buf, const int size,
               const long long pos )
  {
  if( lseek( fd, pos, SEEK_SET ) == pos )
    return readblock( fd, buf, size );
  return 0;
  }

} // end namespace


void Lzip_index::set_errno_error( const char * const msg )
  {
  error_ = msg; error_ += std::strerror( errno );
  retval_ = 1;
  }

void Lzip_index::set_num_error( const char * const msg, unsigned long long num )
  {
  char buf[80];
  snprintf( buf, sizeof buf, "%s%llu", msg, num );
  error_ = buf;
  retval_ = 2;
  }


// If successful, push last member and set pos to member header.
bool Lzip_index::skip_trailing_data( const int fd, long long & pos,
                   const bool ignore_trailing, const bool loose_trailing )
  {
  enum { block_size = 16384,
         buffer_size = block_size + Lzip_trailer::size - 1 + Lzip_header::size };
  uint8_t buffer[buffer_size];
  if( pos < min_member_size ) return false;
  int bsize = pos % block_size;			// total bytes in buffer
  if( bsize <= buffer_size - block_size ) bsize += block_size;
  int search_size = bsize;			// bytes to search for trailer
  int rd_size = bsize;				// bytes to read from file
  unsigned long long ipos = pos - rd_size;	// aligned to block_size

  while( true )
    {
    if( seek_read( fd, buffer, rd_size, ipos ) != rd_size )
      { set_errno_error( "Error seeking member trailer: " ); return false; }
    const uint8_t max_msb = ( ipos + search_size ) >> 56;
    for( int i = search_size; i >= Lzip_trailer::size; --i )
      if( buffer[i-1] <= max_msb )	// most significant byte of member_size
        {
        const Lzip_trailer & trailer =
          *(const Lzip_trailer *)( buffer + i - Lzip_trailer::size );
        const unsigned long long member_size = trailer.member_size();
        if( member_size == 0 )			// skip trailing zeros
          { while( i > Lzip_trailer::size && buffer[i-9] == 0 ) --i; continue; }
        if( member_size > ipos + i || !trailer.verify_consistency() )
          continue;
        Lzip_header header;
        if( seek_read( fd, header.data, Lzip_header::size,
                       ipos + i - member_size ) != Lzip_header::size )
          { set_errno_error( "Error reading member header: " ); return false; }
        const unsigned dictionary_size = header.dictionary_size();
        if( !header.verify_magic() || !header.verify_version() ||
            !isvalid_ds( dictionary_size ) ) continue;
        if( (*(const Lzip_header *)( buffer + i )).verify_prefix( bsize - i ) )
          { error_ = "Last member in input file is truncated or corrupt.";
            retval_ = 2; return false; }
        if( !loose_trailing && bsize - i >= Lzip_header::size &&
            (*(const Lzip_header *)( buffer + i )).verify_corrupt() )
          { error_ = corrupt_mm_msg; retval_ = 2; return false; }
        if( !ignore_trailing )
          { error_ = trailing_msg; retval_ = 2; return false; }
        pos = ipos + i - member_size;
        member_vector.push_back( Member( 0, trailer.data_size(), pos,
                                         member_size, dictionary_size ) );
        return true;
        }
    if( ipos <= 0 )
      { set_num_error( "Bad trailer at pos ", pos - Lzip_trailer::size );
        return false; }
    bsize = buffer_size;
    search_size = bsize - Lzip_header::size;
    rd_size = block_size;
    ipos -= rd_size;
    std::memcpy( buffer + rd_size, buffer, buffer_size - rd_size );
    }
  }


Lzip_index::Lzip_index( const int infd, const bool ignore_trailing,
                        const bool loose_trailing )
  : insize( lseek( infd, 0, SEEK_END ) ), retval_( 0 )
  {
  if( insize < 0 )
    { set_errno_error( "Input file is not seekable: " ); return; }
  if( insize < min_member_size )
    { error_ = "Input file is too short."; retval_ = 2; return; }
  if( insize > INT64_MAX )
    { error_ = "Input file is too long (2^63 bytes or more).";
      retval_ = 2; return; }

  Lzip_header header;
  if( seek_read( infd, header.data, Lzip_header::size, 0 ) != Lzip_header::size )
    { set_errno_error( "Error reading member header: " ); return; }
  if( !header.verify_magic() )
    { error_ = bad_magic_msg; retval_ = 2; return; }
  if( !header.verify_version() )
    { error_ = bad_version( header.version() ); retval_ = 2; return; }
  if( !isvalid_ds( header.dictionary_size() ) )
    { error_ = bad_dict_msg; retval_ = 2; return; }

  long long pos = insize;	// always points to a header or to EOF
  while( pos >= min_member_size )
    {
    Lzip_trailer trailer;
    if( seek_read( infd, trailer.data, Lzip_trailer::size,
                   pos - Lzip_trailer::size ) != Lzip_trailer::size )
      { set_errno_error( "Error reading member trailer: " ); break; }
    const unsigned long long member_size = trailer.member_size();
    if( member_size > (unsigned long long)pos || !trailer.verify_consistency() )
      {
      if( member_vector.empty() )
        { if( skip_trailing_data( infd, pos, ignore_trailing, loose_trailing ) )
            continue; else return; }
      set_num_error( "Bad trailer at pos ", pos - Lzip_trailer::size );
      break;
      }
    if( seek_read( infd, header.data, Lzip_header::size,
                   pos - member_size ) != Lzip_header::size )
      { set_errno_error( "Error reading member header: " ); break; }
    const unsigned dictionary_size = header.dictionary_size();
    if( !header.verify_magic() || !header.verify_version() ||
        !isvalid_ds( dictionary_size ) )
      {
      if( member_vector.empty() )
        { if( skip_trailing_data( infd, pos, ignore_trailing, loose_trailing ) )
            continue; else return; }
      set_num_error( "Bad header at pos ", pos - member_size );
      break;
      }
    pos -= member_size;
    member_vector.push_back( Member( 0, trailer.data_size(), pos,
                                     member_size, dictionary_size ) );
    }
  if( pos != 0 || member_vector.empty() )
    {
    member_vector.clear();
    if( retval_ == 0 ) { error_ = "Can't create file index."; retval_ = 2; }
    return;
    }
  std::reverse( member_vector.begin(), member_vector.end() );
  for( unsigned long i = 0; ; ++i )
    {
    const long long end = member_vector[i].dblock.end();
    if( end < 0 || end > INT64_MAX )
      {
      member_vector.clear();
      error_ = "Data in input file is too long (2^63 bytes or more).";
      retval_ = 2; return;
      }
    if( i + 1 >= member_vector.size() ) break;
    member_vector[i+1].dblock.pos( end );
    }
  }
