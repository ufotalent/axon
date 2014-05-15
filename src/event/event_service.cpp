#include "event/event_service.hpp"
#include "event/event.hpp"
#include "util/lock.hpp"

using namespace axon::event;

EventService::EventService(axon::service::IOService& io_service): io_service_(io_service) {

}


void EventService::fd_events::add_event(Event* e) {
    int type = e->get_type();
    event_queues[type].push(e);
}

// Notice that the perform() operation of fd_events of the same fd
// is possible to be executed by different threads at the same time,
// appropriate locking is needed
void EventService::fd_events::perform() {
    axon::util::ScopedLock lock(&mutex);
    for (int type = 0; type < Event::EVENT_TYPE_COUNT; type++) {
        if (type & poll_events) {
            auto& queue = event_queues[type];
            while (!queue.empty()) {
                if (queue.front()->perform()) {
                    queue.front()->complete();
                    delete queue.front();
                    queue.pop();
                }
            }
        }
    }
}

void EventService::fd_events::cancal_all() {

}


