#pragma once
#include <pthread.h>
#include <map>
#include <memory>
#include "service/io_service.hpp"
#include "event/event.hpp"

namespace axon {
namespace event {

void* launch_run_loop(void*);
class EventService : public axon::util::Noncopyable {
public:
    ~EventService();

    struct fd_event : public std::enable_shared_from_this<fd_event> {
        typedef std::shared_ptr<fd_event> Ptr;

        void perform(uint32_t events);
        void cancel_all();
        void add_event(Event::Ptr);
        
        fd_event(int nfd, axon::service::IOService* nservice):fd(nfd), closed_(false) {
            io_service = nservice;
            pthread_mutex_init(&mutex, NULL);
        }
        virtual ~fd_event() {
            pthread_mutex_destroy(&mutex);
        }

        const int fd;
        pthread_mutex_t mutex;
        int polled_events;
        axon::service::IOService* io_service;
        std::queue<Event::Ptr> event_queues[Event::EVENT_TYPE_COUNT];
        bool closed_;


        struct on_exit_remove_work {
            on_exit_remove_work(axon::service::IOService* nservice): io_service_(nservice) {}
            ~on_exit_remove_work() {
                io_service_->remove_work();
            }
            axon::service::IOService* io_service_;
        };
    };
    
    void register_fd(int fd, fd_event::Ptr event);
    void unregister_fd(int fd, fd_event::Ptr event);
    void start_event(Event::Ptr event, fd_event::Ptr fd_ev);
    friend void* launch_run_loop(void*);

    // Note that static initializer is synchronized after c++11
    static EventService& get_instance() {
        static EventService instance;
        return instance;
    } 

private:
    EventService();
    void start();
    void run_loop();
    void stop();
    void interrupt();

    int epoll_fd_;
    pthread_t run_thread;
    bool closed_;
    int interrupt_fd_[2];


    // std::vector<fd_event*> fd_events_;
    // fd_event* create_fd_event(int fd);
    

    // This mutex is used to ensure unregister_fd and single run to 
    // execute concurently, otherwise the polled fd may have be freed
    pthread_mutex_t cleanup_mutex_;
    std::set<fd_event::Ptr> fd_event_cleanup_;
};

}
}
