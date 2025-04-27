#include "tcp_receiver.hh"
#include <algorithm> // for std::min
#include <cstdint>
#include <limits> // for UINT16_MAX

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Drop the message and byte_stream set error if reset flag is set
  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }

  if ( !_isn.has_value() ) {
    if ( !message.SYN )
      return;
    _isn = message.seqno;
  }

  // Unwrap this message's seqno to an absolute 64-bit number.
  // We use current "writer-cursor+1" as the checkpoint.
  uint64_t checkpoint = reassembler_.writer().bytes_pushed() + 1;
  uint64_t abs_seqno = message.seqno.unwrap( *_isn, checkpoint );

  // If the segment is a SYN, we need to set the first index to 0 for the byte stream.
  // The following segment doesn't have a SYN, but we still need to -1 for the first segment's SYN.
  uint64_t first_index = message.SYN ? 0 : abs_seqno - 1;

  reassembler_.insert( first_index, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage msg;

  if ( reassembler_.writer().has_error() ) {
    // If the stream has been reset, we need to send a RST flag.
    msg.RST = true;
    return msg;
  }

  msg.RST = false;

  // 1) If we’ve seen the SYN, _isn holds our zero point.
  if ( _isn.has_value() ) {
    // bytes_written() is how many payload bytes we have assembled so far.
    // +1 accounts for the SYN sequence‐number itself.
    uint64_t abs_ack = reassembler_.writer().bytes_pushed() + 1;

    if ( reassembler_.writer().is_closed() ) {
      // If the stream is closed, we need to add 1 to the absolute ACK
      // to account for the FIN sequence number.
      abs_ack++;
    }

    // wrap that absolute ACK back into 32 bits relative to _isn
    msg.ackno = Wrap32::wrap( abs_ack, *_isn );
  }

  // 2) Window size = how much more we can buffer
  size_t avail = reassembler_.writer().available_capacity();
  msg.window_size = static_cast<uint16_t>( std::min( avail, size_t( std::numeric_limits<uint16_t>::max() ) ) );

  return msg;
}
