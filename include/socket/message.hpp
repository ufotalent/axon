#pragma once
#include <stdint.h>
#include <vector>
namespace axon {
namespace socket {

class Message {
public:
    struct MessageHeader {
        char signature[8];
        uint32_t content_length;
        uint32_t token;
    };
    const static char AXON_MESSAGE_SIGNATURE[8];
    Message();
    // create an empty message
    Message(uint32_t content_length);

    // create from raw data, including header
    Message(const char* data, uint32_t len);

    void set_data(const char* data, uint32_t len);

    MessageHeader* header() { return reinterpret_cast<MessageHeader*>(&holder_[0]);}
    const MessageHeader* header() const { return reinterpret_cast<const MessageHeader*>(&holder_[0]);}
    char* content_ptr() { return (&holder_[0]) + sizeof(MessageHeader);}
    uint32_t content_length() const { return holder_.size() - sizeof(MessageHeader);}

    char* data() { return &holder_[0]; }
    uint32_t length() const { return holder_.size(); }

    bool valid() const;
private:
    std::vector<char> holder_;
};

}
}
