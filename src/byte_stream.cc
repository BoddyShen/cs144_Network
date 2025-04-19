#include "byte_stream.hh"
#include <stdexcept>

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity )
{
  buffer_.reserve( capacity_ );
}

void Writer::push( string data )
{
  auto can_take = std::min<uint64_t>( data.size(), capacity_ - ( buffer_.size() - head_ ) );
  buffer_.append( data.data(), can_take );
  bytes_pushed_ += can_take;
}

void Writer::close()
{
  closed_ = true;
}

bool Writer::is_closed() const
{
  return closed_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - ( buffer_.size() - head_ );
}

uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_;
}

// peek() will return a string_view that points to the buffer data.
// Tests will call bytes_buffered() to know what the current bytes in buffer.
string_view Reader::peek() const
{
  return { buffer_.data() + head_, buffer_.size() - head_ };
}

void Reader::pop( uint64_t len )
{
  size_t available = buffer_.size() - head_;
  if ( len > available )
    throw std::runtime_error( "pop too large" );
  head_ += len;
  bytes_popped_ += len;

  // compact once we've consumed a chunk to avoid unbounded head_
  if ( head_ > buffer_.capacity() * 3 / 4 ) {
    buffer_.erase( 0, head_ );
    head_ = 0;
  }
}

bool Reader::is_finished() const
{
  return closed_ && buffer_.size() == head_;
}

uint64_t Reader::bytes_buffered() const
{
  return buffer_.size() - head_;
}

uint64_t Reader::bytes_popped() const
{
  return bytes_popped_;
}
