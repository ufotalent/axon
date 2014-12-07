#include "socket/message_socket.hpp"
#include <cstring>

using namespace axon::socket;
const char Message::AXON_MESSAGE_SIGNATURE[8] = {'A', 'X', 'O', 'N', 'M', 'S', 'G', 0};
Message::Message() :
    Message(0) {
}

Message::Message(uint32_t content_length) {
    set_size(content_length);
}


Message::Message(const char* data, uint32_t len):
    holder_(data, data + len) {
}

void Message::set_size(uint32_t content_length) {
    holder_.resize(content_length + sizeof(MessageHeader));
    MessageHeader *header_ = header();
    memcpy(header_->signature, AXON_MESSAGE_SIGNATURE, sizeof(AXON_MESSAGE_SIGNATURE));
    header_->content_length = content_length;
}

void Message::set_data(const char* data, uint32_t len) {
    holder_ = std::vector<char>(data, data + len);
}

bool Message::valid() const {
    if (length() < sizeof(MessageHeader)) {
        return false;
    }
    const MessageHeader *header_ = header();
    return (header_->content_length == content_length()) && (memcmp(AXON_MESSAGE_SIGNATURE, header_->signature, sizeof(AXON_MESSAGE_SIGNATURE)) == 0);
}
