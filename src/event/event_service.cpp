#include "event/event_service.hpp"
#include "event/event.hpp"
#include "util/lock.hpp"
#include <sys/epoll.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

using namespace axon::event;

EventService::EventService():closed_(false) {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("epoll creation failed");
    }
    if (pipe2(interrupt_fd_, O_NONBLOCK) < 0) {
        throw std::runtime_error("interrupter creation failed");
    }
    // register interrupter 
    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = &interrupt_fd_;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, interrupt_fd_[0], &ev) != 0)  {
        throw std::runtime_error("interrupter registeration failed");
    }
    start();
}

EventService::~EventService() {
    stop();
    /*
    for (auto it = fd_events_.begin(); it != fd_events_.end(); it++) {
        delete (*it);
    }
    fd_events_.clear();
    */
    // pthread_mutex_destroy(&fd_event_creation_mutex_);

}

bool EventService::register_fd(int fd, fd_event* event) {
    epoll_event ev;
    // EPOLLIN must be set when registering, otherwise events before ctl with EPOLLIN will be lost
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = (void*) event;
    event->polled_events = ev.events;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) != 0)  {
        throw std::runtime_error("register fd failed");
        return false;
    }
    return true;
}

void EventService::start_event(Event::Ptr event, fd_event* fd_ev) { 
    // Some data may have arrived before event started, try performing
    if (event->perform()) {
        fd_ev->io_service->post(std::bind(&Event::complete, event));
        return;
    }

    axon::util::ScopedLock lock(&fd_ev->mutex);
    // Read is already registered, simply ignore and register write
    if (event->get_type() == Event::EVENT_TYPE_WRITE) {
        if ((fd_ev->polled_events & EPOLLOUT) == 0) {
            epoll_event ev;
            ev.events = fd_ev->polled_events | EPOLLOUT | EPOLLET;
            ev.data.ptr = (void*) fd_ev;
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd_ev->fd, &ev) == 0) {
                fd_ev->polled_events |= EPOLLOUT;
            } else {
                throw std::runtime_error("epoll ctl failed");
            }
        }
    }
    fd_ev->add_event(event);
    fd_ev->io_service->add_work();
}

void* launch_run_loop(void* args) {
    EventService *service = static_cast<EventService*>(args);
    service->run_loop();
    return NULL;
}
void EventService::start() {
    pthread_create(&run_thread, NULL, &launch_run_loop, this);
}
void EventService::stop() {
    closed_ = true;
    interrupt();
    pthread_join(run_thread, NULL);
}
void EventService::interrupt() {
    char b;
    write(interrupt_fd_[1], &b, 1);
}

void EventService::run_loop() {
    while (true) {
        epoll_event evs[128];
        int cnt = epoll_wait(epoll_fd_, evs, 128, -1);
        if (cnt < 0) {
            perror("poll failed");
        }
        for (int i = 0; i < cnt; i++) {
            if (evs[i].data.ptr == &interrupt_fd_) {
                // drain interrupter pipe
                char buf[256];
                while (read(interrupt_fd_[0], buf, 256) > 0);
                continue;
            }
            fd_event* fd_ev = (fd_event*) evs[i].data.ptr;
            uint32_t events = evs[i].events;
            fd_ev->io_service->post(std::bind(&fd_event::perform, fd_ev, events));
        }
        if (closed_) {
            return;
        }
    }
}
/*
EventService::fd_event* EventService::create_fd_event(int fd) {
    axon::util::ScopedLock lock(&fd_event_creation_mutex_);
    fd_event *p = new fd_event(fd);
    fd_events_.push_back(p);
    return p;
}

*/

void EventService::fd_event::add_event(Event::Ptr e) {
    int type = e->get_type();
    event_queues[type].push(e);
}

// Notice that the perform() operation of fd_event of the same fd
// is possible to be executed by different threads at the same time,
// appropriate locking is needed
void EventService::fd_event::perform(uint32_t events) {
    axon::util::ScopedLock lock(&mutex);
    int flag[Event::EVENT_TYPE_COUNT] = {EPOLLIN, EPOLLOUT, EPOLLPRI};
    for (int type = Event::EVENT_TYPE_COUNT - 1; type >= 0; type--) {
        if (flag[type] & events) {
            auto& queue = event_queues[type];
            while (!queue.empty()) {
                if (queue.front()->perform()) {
                    Event::Ptr done_ev = queue.front();
                    axon::service::IOService *service = io_service;
                    service->post([done_ev, service]{
                        // Exception may be thrown from completion handler, ensure work removed
                        on_exit_remove_work r(service);
                        done_ev->complete();
                    });
                    queue.pop();
                } else {
                    break;
                }
            }
        }
    }
}

void EventService::fd_event::cancal_all() {

}

