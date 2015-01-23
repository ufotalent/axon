#include "event/event_service.hpp"
#include "event/event.hpp"
#include "util/lock.hpp"
#include <sys/epoll.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

using namespace axon::event;

EventService::EventService():closed_(false) {
    pthread_mutex_init(&cleanup_mutex_, NULL);
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
    pthread_mutex_destroy(&cleanup_mutex_);
    /*
    for (auto it = fd_events_.begin(); it != fd_events_.end(); it++) {
        delete (*it);
    }
    fd_events_.clear();
    */
    // pthread_mutex_destroy(&fd_event_creation_mutex_);

}

void EventService::register_fd(int fd, fd_event::Ptr event) {
    epoll_event ev;
    // EPOLLIN must be set when registering, otherwise events before ctl with EPOLLIN will be lost
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = (void*) event.get();
    event->polled_events = ev.events;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) != 0)  {
        throw std::runtime_error("register fd failed");
    }
}

void EventService::unregister_fd(int fd, fd_event::Ptr event) {

    // remove from epoll and cancell all handlers
    {
        axon::util::ScopedLock lock2(&event->mutex);
        epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ev);
        event->closed_ = true;
        event->cancel_all();
    }

    // wait for run to finish
    axon::util::ScopedLock lock1(&cleanup_mutex_);
    fd_event_cleanup_.insert(event);
    // notice run loop to process list
    interrupt();
}

void EventService::start_event(Event::Ptr event, fd_event::Ptr fd_ev) { 
    axon::util::ScopedLock lock(&fd_ev->mutex);
    if (fd_ev->closed_) {
        if (event->callback_strand()) {
            event->callback_strand()->post(std::bind(&Event::complete, event));
        } else {
            fd_ev->io_service->post(std::bind(&Event::complete, event));
        }
        return;
    }

    // Some data may have arrived before event started, try performing
    if (event->should_pre_try() && event->perform()) {
        if (event->callback_strand()) {
            event->callback_strand()->post(std::bind(&Event::complete, event));
        } else {
            fd_ev->io_service->post(std::bind(&Event::complete, event));
        }
        return;
    }

    // Read is already registered, simply ignore and register write
    if (event->get_type() == Event::EVENT_TYPE_WRITE) {
        if ((fd_ev->polled_events & EPOLLOUT) == 0) {
            epoll_event ev;
            ev.events = fd_ev->polled_events | EPOLLOUT | EPOLLET;
            ev.data.ptr = (void*) fd_ev.get();
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

void* axon::event::launch_run_loop(void* args) {
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
    char b = 0;
    write(interrupt_fd_[1], &b, 1);
}

void EventService::run_loop() {
    while (true) {
        epoll_event evs[128];
        int cnt = epoll_wait(epoll_fd_, evs, 128, -1);
        if (cnt < 0) {
            // perror("poll failed");
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
            fd_ev->io_service->post(std::bind(&fd_event::perform, fd_ev->shared_from_this(), events));
        }
        if (closed_) {
            return;
        }

        // release pointers in fd_event_cleanup_
        axon::util::ScopedLock lock1(&cleanup_mutex_);
        fd_event_cleanup_.clear();
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
                    axon::service::IOService *service = io_service;
                    Event::Ptr done_ev = queue.front();

                    auto completion = [done_ev, service]{
                        // Exception may be thrown from completion handler, ensure work removed
                        on_exit_remove_work r(service);
                        done_ev->complete();
                    };
                    if (done_ev->callback_strand()) {
                        done_ev->callback_strand()->post(std::move(completion));
                    } else {
                        service->post(std::move(completion));
                    }
                    queue.pop();
                } else {
                    break;
                }
            }
        }
    }
}

void EventService::fd_event::cancel_all() {
    for (int type = 0; type < Event::EVENT_TYPE_COUNT; type++) {
        auto& queue = event_queues[type];
        while (!queue.empty()) {
            // post complete without performing
            axon::service::IOService *service = io_service;
            Event::Ptr done_ev = queue.front();

            auto completion = [done_ev, service]{
                // Exception may be thrown from completion handler, ensure work removed
                on_exit_remove_work r(service);
                done_ev->complete();
            };
            if (done_ev->callback_strand()) {
                done_ev->callback_strand()->post(std::move(completion));
            } else {
                service->post(std::move(completion));
            }
            queue.pop();
        }
    }

}

