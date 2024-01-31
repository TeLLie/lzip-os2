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

#ifndef INT64_MAX
#define INT64_MAX 0x7FFFFFFFFFFFFFFFLL
#endif


class Block
  {
  long long pos_, size_;	// pos >= 0, size >= 0, pos + size <= INT64_MAX

public:
  Block( const long long p, const long long s ) : pos_( p ), size_( s ) {}

  long long pos() const { return pos_; }
  long long size() const { return size_; }
  long long end() const { return pos_ + size_; }

  void pos( const long long p ) { pos_ = p; }
  void size( const long long s ) { size_ = s; }
  };


class Lzip_index
  {
  struct Member
    {
    Block dblock, mblock;		// data block, member block
    unsigned dictionary_size;

    Member( const long long dpos, const long long dsize,
            const long long mpos, const long long msize,
            const unsigned dict_size )
      : dblock( dpos, dsize ), mblock( mpos, msize ),
        dictionary_size( dict_size ) {}
    };

  std::vector< Member > member_vector;
  std::string error_;
  const long long insize;
  int retval_;
  unsigned dictionary_size_;	// largest dictionary size in the file

  bool check_header( const Lzip_header & header );
  void set_errno_error( const char * const msg );
  void set_num_error( const char * const msg, unsigned long long num );
  bool read_header( const int fd, Lzip_header & header, const long long pos,
                    const bool ignore_marking );
  bool skip_trailing_data( const int fd, unsigned long long & pos,
                           const Cl_options & cl_opts );

public:
  Lzip_index( const int infd, const Cl_options & cl_opts );

  long members() const { return member_vector.size(); }
  const std::string & error() const { return error_; }
  int retval() const { return retval_; }
  unsigned dictionary_size() const { return dictionary_size_; }

  long long udata_size() const
    { if( member_vector.empty() ) return 0;
      return member_vector.back().dblock.end(); }

  long long cdata_size() const
    { if( member_vector.empty() ) return 0;
      return member_vector.back().mblock.end(); }

  // total size including trailing data (if any)
  long long file_size() const
    { if( insize >= 0 ) return insize; else return 0; }

  const Block & dblock( const long i ) const
    { return member_vector[i].dblock; }
  const Block & mblock( const long i ) const
    { return member_vector[i].mblock; }
  unsigned dictionary_size( const long i ) const
    { return member_vector[i].dictionary_size; }
  };
