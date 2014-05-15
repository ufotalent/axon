#pragma once
#include <pthread.h>
#include <map>
#include "service/io_service.hpp"
#include "event/event.hpp"
namespace axon {
namespace event {

class EventService {
public:
    EventService(axon::service::IOService& io_service);
    bool register_event(const Event* event);
    void run();
    void stop();

    struct fd_events {
        int poll_events;
        std::queue<Event*> event_queues[Event::EVENT_TYPE_COUNT];
        void perform();
        void cancal_all();
        void add_event(Event*);
        
        fd_events() {
            pthread_mutex_init(&mutex, NULL);
        }
        virtual ~fd_events() {
            pthread_mutex_destroy(&mutex);
        }
        pthread_mutex_t mutex;

    };
private:
    axon::service::IOService& io_service_;
};

}
}
