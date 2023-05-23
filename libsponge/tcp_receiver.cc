#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // If receiver hasn't received SYN and the incomming segment 
    // doesn't contain a SYN flag, simply return.
    if (!_syn_rcvd && !seg.header().syn) return;

    // If the incomming segment contains a SYN flag, initialize isn.
    if (!_syn_rcvd && seg.header().syn) {
        _syn_rcvd = true;
        _isn = seg.header().seqno.raw_value();
    }

    // Get checkpoint.
    size_t _checkpoint = _reassembler.stream_out().bytes_written();

    string _data = seg.payload().copy();
    uint64_t _index = unwrap(seg.header().seqno, WrappingInt32(_isn), _checkpoint) - (!seg.header().syn);
    _reassembler.push_substring(_data, _index, seg.header().fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_syn_rcvd) {
        return std::nullopt;
    } else {
        size_t _bytes_written = _reassembler.stream_out().bytes_written();
        bool _input_ended = _reassembler.stream_out().input_ended();
        return wrap(_syn_rcvd + _bytes_written + _input_ended, WrappingInt32(_isn));
    }
}

size_t TCPReceiver::window_size() const {
    size_t _bytes_written = _reassembler.stream_out().bytes_written();
    size_t _bytes_read = _reassembler.stream_out().bytes_read();
    return _capacity - (_bytes_written - _bytes_read);
}
