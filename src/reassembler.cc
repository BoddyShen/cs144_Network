#include "reassembler.hh"
#include "iostream"

using namespace std;

// Merge (first_unassembled_index, first_unacceptable_index] to  _segments
// and return the leftmost index of the merged segment
pair<bool, uint64_t> Reassembler::_merge( uint64_t left_index, string data )
{
  if ( data.size() == 0 ) {
    return { false, left_index };
  }

  auto it = _segments.lower_bound( left_index ); // Find the first segment that starts after or at left_index
  uint64_t right_index = left_index + data.size();

  // merge forward
  if ( it != _segments.begin() ) {
    auto prev_it = prev( it );
    while ( prev_it->first + prev_it->second.size() > left_index ) {
      uint64_t prev_left_index = prev_it->first;
      uint64_t prev_right_index = prev_it->first + prev_it->second.size();
      string prev_data = prev_it->second;

      if ( prev_right_index >= right_index )
        return { false, left_index };

      data = prev_data + data.substr( prev_right_index - left_index, right_index - prev_right_index );
      left_index = min( left_index, prev_left_index );

      prev_it = _segments.erase( prev_it );
      _bytes_pending -= prev_data.size();
      if ( prev_it == _segments.begin() )
        break;
      prev_it--;
    }
  }

  // merge backward
  while ( it != _segments.end() && it->first <= right_index ) {
    uint64_t it_left_index = it->first;
    uint64_t it_right_index = it->first + it->second.size();
    string it_data = it->second;

    if ( it_right_index <= right_index ) {
      it = _segments.erase( it );
      _bytes_pending -= it_data.size();
      continue;
    }

    data = data + it_data.substr( right_index - it_left_index, it_right_index - right_index );
    right_index = max( right_index, it_right_index );

    it = _segments.erase( it );
    _bytes_pending -= it_data.size();
  }
  _segments[left_index] = data;
  return { true, left_index };
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // If this is the last substring, mark it
  if ( is_last_substring ) {
    _eof_seen = true;
    _eof_index = first_index + data.size();
    if ( data.size() == 0 ) {
      output_.writer().close();
      return;
    }
  }

  // Update first_unpopped_index, first_unacceptable_index, and first_unassembled_index
  uint64_t first_unpopped_index = output_.reader().bytes_popped();
  uint64_t first_unacceptable_index = first_unpopped_index + output_.get_capacity();
  first_unassembled_index = first_unpopped_index + output_.get_capacity() - output_.writer().available_capacity();

  if ( first_index >= first_unacceptable_index ) {
    return; // Ignore data that is beyond the capacity
  }

  // Check if the data is within the capacity
  if ( first_index + data.size() <= first_unassembled_index ) {
    return; // Ignore data that is beyond the capacity
  }

  uint64_t left_index = max( first_index, first_unassembled_index );
  uint64_t right_index = min( first_index + data.size(), first_unacceptable_index );
  data = data.substr( left_index - first_index, right_index - left_index );

  if ( data.size() == 0 ) {
    return; // Ignore empty data
  }

  auto merge_result = _merge( left_index, data );
  bool valid = merge_result.first;
  if ( !valid ) {
    return; // Ignore merged data if it is not valid
  }
  left_index = merge_result.second;
  _bytes_pending += _segments[left_index].size();

  // Update first_unassembled_index
  while ( _segments.count( first_unassembled_index )
          && output_.writer().available_capacity() >= _segments[first_unassembled_index].size() ) {
    output_.writer().push( _segments[first_unassembled_index] );

    if ( _eof_seen && first_unassembled_index + _segments[first_unassembled_index].size() == _eof_index ) {
      output_.writer().close();
    }

    if ( _segments[first_unassembled_index].size() == 0 )
      break;

    _bytes_pending -= _segments[first_unassembled_index].size();

    uint64_t remove_index = first_unassembled_index;
    first_unassembled_index += _segments[first_unassembled_index].size();
    _segments.erase( remove_index );
  };
}

uint64_t Reassembler::bytes_pending() const
{
  return _bytes_pending;
}
