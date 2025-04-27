#include "wrapping_integers.hh"
#include "iostream"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return Wrap32( static_cast<uint32_t>( n ) + zero_point.raw_value_ );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  uint32_t offset = raw_value_ - zero_point.raw_value_;
  uint32_t checkpoint_offset = checkpoint % ( 1UL << 32 );
  uint64_t ret = 0;

  uint32_t diff = offset - checkpoint_offset;

  if ( diff >= ( 1UL << 31 ) && ( checkpoint + diff ) >= ( 1UL << 32 ) )
    ret = checkpoint + diff - ( 1UL << 32 );
  else
    ret = checkpoint + diff;
  return ret;
}
