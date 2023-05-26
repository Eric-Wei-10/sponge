#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _next_ackno; }

void TCPSender::fill_window() {
    // If SYN hasn't been sent.
    if (!_syn_sent) {
        TCPSegment _segment;

        _segment.header().syn = true;
        _syn_sent = true;
        _segment.header().seqno = _isn;

        // Push to queue.
        _segments_out.push(_segment);
        _outstanding.push(_segment);

        // Start timer.
        if (!_timer_started) {
            _timer_started = true;
            _consecutive_retransmissions = 0;
            _timer_countdown = _initial_retransmission_timeout;
        }

        // Update '_next_seqno' AND '_upper_bound'.
        _next_seqno = _upper_bound = 1;

        return;
    }

    if (_fin_sent)
        return;

    // Send one byte if the receiver announces its window size to be 0.
    if ((_upper_bound == _next_ackno ) && !_canary_sent) {
        cout << "send a canary" << endl;
        TCPSegment _segment;
        
        _segment.header().seqno = wrap(_next_seqno, _isn);

        if (!_stream.buffer_empty()) {
            // If buffer is not empty, send a byte.
            _segment.payload() = Buffer(_stream.peek_output(1));
            _stream.pop_output(1);
        } else {
            // If buffer is empty and at eof, send fin.
            if (_stream.eof()) {
                _segment.header().fin = true;
                _fin_sent = true;
            }
        }

        // Push segment into queue.
        _segments_out.push(_segment);
        _outstanding.push(_segment);
        _canary_sent = true;

        // Start timer.
        if (!_timer_started) {
            _timer_started = true;
            _consecutive_retransmissions = 0;
            _timer_countdown = _initial_retransmission_timeout;
        }

        return;
    }

    // Send a normal segment according to buffer and window size.
    size_t _bytes_can_send = _upper_bound - _next_seqno;
    size_t _bytes_to_read = _stream.buffer_size();

    if (_bytes_can_send == 0) {
        // Nothing to send.
        return;
    }

    if (_bytes_to_read == 0 && !_stream.input_ended()) {
        // If the buffer is empty and the stream is not ended, return immediately.
        return;
    } else if (_bytes_to_read == 0 && _stream.input_ended()) {
        // Send a segment with merely a FIN flag.
        TCPSegment _segment;
        _segment.header().seqno = wrap(_next_seqno, _isn);
        _segment.header().fin = true;
        _fin_sent = true;
        _next_seqno += 1;

        // Push segment into queue.
        _segments_out.push(_segment);
        _outstanding.push(_segment);

        // Start timer.
        if (!_timer_started) {
            _timer_started = true;
            _consecutive_retransmissions = 0;
            _timer_countdown = _initial_retransmission_timeout;
        }

        return;
    }

    size_t _bytes_to_send = _bytes_can_send < _bytes_to_read ? _bytes_can_send : _bytes_to_read;

    while (_bytes_to_send) {
        TCPSegment _segment;
        size_t _payload_size =
            _bytes_to_send < TCPConfig::MAX_PAYLOAD_SIZE ? _bytes_to_send : TCPConfig::MAX_PAYLOAD_SIZE;

        // Fill header.
        _segment.header().seqno = wrap(_next_seqno, _isn);
        // Get payload string.
        _segment.payload() = Buffer(_stream.peek_output(_payload_size));
        _stream.pop_output(_payload_size);
        _next_seqno += _payload_size;

        // Set FIN flag if there is enough space.
        if (_payload_size < _bytes_can_send && _stream.eof()) {
            _segment.header().fin = true;
            _fin_sent = true;
            _next_seqno += 1;
            // cout << "send fin" << endl;
        }

        // Push segment into queue.
        _segments_out.push(_segment);
        _outstanding.push(_segment);

        // Start timer.
        if (!_timer_started) {
            _timer_started = true;
            _consecutive_retransmissions = 0;
            _timer_countdown = _initial_retransmission_timeout;
        }

        _bytes_can_send -= _payload_size;
        _bytes_to_send -= _payload_size;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t _abs_ackno = unwrap(ackno, _isn, _next_ackno);

    // Ignore illegal and outdated ACK package.
    if (!_canary_sent && (_abs_ackno > _upper_bound || _abs_ackno < _next_ackno))
        /*
         * The line below is also correct (even more efficient), but there is a
         * bug in test 'send_transmit'. See send_transmit.cc: 96 for more information.
         * */
        // if (!_canary_sent && (_abs_ackno > _upper_bound || _abs_ackno + window_size < _upper_bound))
        return;

    if (_canary_sent) {
        cout << "ack a canary" << endl;
        // ack at eof.
        _next_seqno += 1;
        _canary_sent = false;
    }

    // Remove ongoing segment.
    while (!_outstanding.empty()) {
        TCPSegment _seg = _outstanding.front();
        uint64_t _abs_seqno = unwrap(_seg.header().seqno, _isn, _next_seqno);
        size_t _segment_size = _seg.length_in_sequence_space();

        if (_abs_seqno + _segment_size <= _abs_ackno) {
            _outstanding.pop();
            // cout << "poped segment begins at: " << _abs_seqno << endl;
        } else {
            break;
        }
    }

    // If new bytes are ACKed, adjust timer.
    if (_abs_ackno > _next_ackno) {
        if (_outstanding.empty()) {
            _timer_started = false;
        } else {
            _consecutive_retransmissions = 0;
            _timer_countdown = _initial_retransmission_timeout;
        }
    }

    // Update internal state.
    _next_ackno = _abs_ackno;
    _upper_bound = _abs_ackno + window_size;

    return;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer_started) return;

    size_t _remaining_time_ticks = ms_since_last_tick;
    bool _resend = false;

    while (_remaining_time_ticks >= _timer_countdown) {
        _remaining_time_ticks -= _timer_countdown;

        _consecutive_retransmissions += 1;
        if (_consecutive_retransmissions > TCPConfig::MAX_RETX_ATTEMPTS) {
            // If retx times exceed the limit, stop timer and return.
            _timer_started = false;
            return;
        }

        // The canary segment doesn't double rto.
        if (_canary_sent) {
            _timer_countdown = _initial_retransmission_timeout;
        } else {
            _timer_countdown = (_initial_retransmission_timeout << _consecutive_retransmissions);
        }

        // Only resend once each time 'tick' is called.
        if (!_resend) {
            _resend = true;
            _segments_out.push(_outstanding.front());
        }
    }

    _timer_countdown -= _remaining_time_ticks;
}

unsigned int TCPSender::consecutive_retransmissions() const {
    return _consecutive_retransmissions;
}

void TCPSender::send_empty_segment() {}
