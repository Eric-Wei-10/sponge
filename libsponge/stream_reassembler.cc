#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity)
    , _capacity(capacity)
    , _unassembled()
    , _first_unread(0)
    , _first_unassembled(0)
    , _first_unaccepted(capacity)
    , _eof_received(false) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // Update internal state.
    _first_unread = _output.bytes_read();
    _first_unassembled = _output.bytes_written();
    _first_unaccepted = _first_unread + _capacity;
    // cout << _first_unread << _first_unassembled << _first_unaccepted << endl;

    string _data = data;
    size_t _index = index;
    bool _eof = eof;
    size_t length = data.length();

    // Skip empty string.
    if (length == 0) {
        if (eof)
            _output.end_input();
        return;
    }
    // Skip string that is entirely out of bound.
    if (index + length <= _first_unassembled || index >= _first_unaccepted)
        return;

    // Crop string that is partly out of bound.
    // Crop head.
    if (index < _first_unassembled) {
        _data = _data.erase(0, _first_unassembled - _index);
        _index = _first_unassembled;
    }
    // Crop tail.
    if (index + length > _first_unaccepted) {
        _data = _data.substr(0, _first_unaccepted - _index);
        _eof = false;  // If the tail is cropped, then the stream can't end.
        // cout << "disable eof" << endl;
    }

    // Insert the string into map.
    auto it = _unassembled.find(_index);
    if (it == _unassembled.end()) {
        // If a string starts at '_index' doesn't exist, create a new pair.
        _unassembled[_index] = _data;
        // cout << "Inserted a new pair" << endl;
    } else {
        // If it exist, replace if the new one is longer.
        if (_data.length() > it->second.length()) {
            _unassembled[_index] = _data;
            // cout << "Inserted a longer pair" << endl;
        }
    }

    // Put continuous data to the output stream.
    size_t first_unassembled = _first_unassembled;
    for (it = _unassembled.begin(); it != _unassembled.end();) {
        if (it->first == first_unassembled) {
            // If the string could be put into stream, put it.
            _output.write(it->second);
            first_unassembled += it->second.length();
            auto tmp = it;
            it++;
            _unassembled.erase(tmp);
        } else if (it->first < first_unassembled) {
            if (it->first + it->second.length() <= first_unassembled) {
                auto tmp = it;
                it++;
                _unassembled.erase(tmp);
            } else {
                _output.write(it->second.substr(first_unassembled - it->first));
                first_unassembled = it->first + it->second.length();
                auto tmp = it;
                it++;
                _unassembled.erase(tmp);
            }
        } else {
            break;
        }
    }

    if (_eof) {
        _eof_received = true;
    }

    if (_eof_received && unassembled_bytes() == 0) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t count = 0;
    size_t max_index = _first_unassembled;

    for (auto it = _unassembled.begin(); it != _unassembled.end(); it++) {
        if (it->first < max_index) {
            if (it->first + it->second.length() <= max_index) {
                continue;
            } else {
                count += it->first + it->second.length() - max_index;
                max_index = it->first + it->second.length();
            }
        } else {
            count += it->second.length();
            max_index = it->first + it->second.length();
        }
    }

    return count;
}

bool StreamReassembler::empty() const { return _output.buffer_empty() && (unassembled_bytes() == 0); }
