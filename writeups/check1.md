Checkpoint 1 Writeup
====================
<img width="1051" alt="Screenshot 2025-04-21 at 3 09 53 AM" src="https://github.com/user-attachments/assets/37eba4c6-c72a-46c6-a6b9-098315a28b49" />

The capacity in the figure is a byte_stream instance’s buffer, and reassembler is on top of it, we can only use reassembler to insert packet.


### Component:

1. `ByteStream output_`:
   - ByteStream can be used as writer and reader in the same address of Bytestream instance, see byte_stream_helpers.cc, so we can use it to writer data, and use reader to get the `first_unpopped_index`
2. `map<uint64_t, std::string> _segments`
   - We need to maintain sorted interval in ByteStream instance buffer.

### Insert process:

1. First update the first*unpopped_index by `output*.reader().bytes_popped()`
2. Get `first_unassembled_index`, `first_unacceptable_index`
3. The Green part () is being pushed, so we don’t care it.
4. The Red part is the rest of buffer, the current inserted data have to get the overlap to the red part first, which is valid data region, the outer part will be discarded as lab description.
5. After we get the valid data, we have to merge it with \_segment, which is the data visited first and lay into red part.
6. We use `_segments.lower_bound` to get the correct insert position in O(log n), and then merge forward and merge backward. We only merge if two are overlapped. When merging, we erase previous start_index (be careful not to use erase(key) here, since we will lose iterator) After merge, we put `_segments[left_index] = data;`
7. The last will try to write new data into buffer, since we have new data may having first_unassembled_index.

