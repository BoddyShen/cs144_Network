#include "tcp_sender.hh"
#include "tcp_config.hh"
#include <algorithm> // for std::min

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return seq_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmissions_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // SYN‑only first segment
  if ( !syn_sent_ ) {
    TCPSenderMessage syn_msg;
    syn_msg.seqno = Wrap32::wrap( next_seqno_abs_, isn_ );
    syn_msg.SYN = true;
    syn_msg.RST = input_.writer().has_error() ? true : false;
    syn_msg.FIN = false;
    syn_msg.payload = "";
    // If the application has already closed the stream, we can also send FIN now:
    if ( input_.reader().is_finished() ) {
      syn_msg.FIN = true;
      fin_sent_ = true;
      // SYN consumes 1, FIN consumes 1 more
      next_seqno_abs_ += 2;
      seq_in_flight_ += 2;
    } else {
      // Only SYN
      next_seqno_abs_ += 1;
      seq_in_flight_ += 1;
    }

    transmit( syn_msg );
    syn_sent_ = true;
    outstanding_messages_.push( syn_msg );
    if ( !timer_running_ ) {
      timer_current_ = 0;
      current_RTO_ms_ = initial_RTO_ms_;
      timer_running_ = true;
    }
    return;
  }

  // pretend window size is 1 if it is 0, which try to get a new ack
  uint64_t window_size = receiver_window_size_ == 0 ? 1 : receiver_window_size_;

  if ( window_size <= seq_in_flight_ ) {
    return; // no space in receiver window
  }

  uint64_t eff_window = window_size - seq_in_flight_;

  while ( eff_window > 0 && !fin_sent_ ) {
    uint64_t max_payload = std::min<uint64_t>( eff_window, TCPConfig::MAX_PAYLOAD_SIZE );
    string payload = "";
    read( input_.reader(), max_payload, payload );

    bool send_fin = false;
    if ( payload.size() < eff_window && !fin_sent_ && input_.reader().is_finished() ) {
      send_fin = true;
      fin_sent_ = true;
    }

    if ( payload.empty() && !send_fin ) {
      break;
    }

    TCPSenderMessage msg;
    msg.SYN = false;
    msg.payload = std::move( payload );
    msg.RST = input_.writer().has_error() ? true : false;
    msg.FIN = send_fin;
    msg.seqno = Wrap32::wrap( next_seqno_abs_, isn_ );
    transmit( msg );

    if ( msg.sequence_length() > 0 ) {
      outstanding_messages_.push( msg );
      seq_in_flight_ += msg.sequence_length();
      next_seqno_abs_ += msg.sequence_length();
      eff_window -= msg.sequence_length();

      if ( !timer_running_ ) {
        timer_current_ = 0;
        current_RTO_ms_ = initial_RTO_ms_;
        timer_running_ = true;
      }
    } else {
      break; // no data, no FIN ⇒ no point looping again
    }

    if ( msg.FIN ) {
      break; // no more payload
    }
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = Wrap32::wrap( next_seqno_abs_, isn_ );
  msg.SYN = false;
  msg.payload = "";
  msg.FIN = false;
  msg.RST = input_.writer().has_error() ? true : false;

  return msg;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{

  if ( msg.RST ) {
    input_.reader().set_error();
    return;
  }

  if ( !msg.ackno.has_value() ) {
    return;
  }

  uint64_t ackno_abs = msg.ackno->unwrap( isn_, next_seqno_abs_ );
  if ( ackno_abs > next_seqno_abs_ ) {
    // impossible ackno
    return;
  }
  receiver_window_size_ = msg.window_size;

  // Discard all messages that are already acknowledged, last sequence number < ackno_abs
  bool acked_new_data = false;
  while ( !outstanding_messages_.empty() ) {
    const TCPSenderMessage& out_msg = outstanding_messages_.front();
    uint64_t seqno_abs = out_msg.seqno.unwrap( isn_, next_seqno_abs_ );
    uint64_t seqno_end_abs = seqno_abs + out_msg.sequence_length();
    if ( seqno_end_abs <= ackno_abs ) {
      seq_in_flight_ -= out_msg.sequence_length();
      outstanding_messages_.pop();
      acked_new_data = true;
    } else {
      break;
    }
  }

  // Reset current_RTO_ms_, consecutive_retransmissions_
  if ( acked_new_data ) {
    current_RTO_ms_ = initial_RTO_ms_;
    consecutive_retransmissions_ = 0;

    if ( outstanding_messages_.empty() ) {
      timer_running_ = false;
    } else {
      timer_current_ = 0;
      timer_running_ = true;
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( !timer_running_ ) {
    return;
  }

  timer_current_ += ms_since_last_tick;

  // check for expiration
  if ( timer_current_ < current_RTO_ms_ ) {
    return;
  }

  if ( consecutive_retransmissions_ > TCPConfig::MAX_RETX_ATTEMPTS ) {
    // retransmission limit reached
    timer_running_ = false;
    return;
  }

  // Since we only care about the oldest transmission, we use queue to store the transmit messages.
  // we transmit message even if the receiver window size is 0 (break receiver_window_size_ = 0 condition)
  // but only if receiver_window_size_ > 0 count as real retransmission
  if ( !outstanding_messages_.empty() ) {
    const TCPSenderMessage& msg = outstanding_messages_.front();
    transmit( msg );

    bool do_backoff = msg.SYN                       // always back off on SYN
                      || receiver_window_size_ > 0; // or if peer actually had window > 0

    if ( do_backoff ) {
      ++consecutive_retransmissions_;
      current_RTO_ms_ *= 2;
    }
    // in *all* cases reset the timer so the next tick starts counting from zero
    timer_current_ = 0;

  } else {
    timer_running_ = false;
  }
}
