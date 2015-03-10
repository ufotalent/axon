libaxon
====

an async io library

Functionalities
----
High Performance: 

* ~250000 QPS (1 server thread)
* ~500000 QPS (4 server thread)


Async IO part:
* async socket event operations: connect, accept, send, send_until, recv, recv_until
* async timer operations
* automatic event muliplexing and callback dispatching
* implicit locks using strand objects, avoiding mutex blockings


RPC part:
* base service class that automaticly accepts and handles incoming connections
* client auto reconnection


Dependency
----
* g++-4.7 and later
* c++11
* scons
* libboost\_context
* gtest(optinal)
* libtcmalloc(optional)

Build
----
* run 'scons' to build
* add option '--no-tcmalloc' or '--no-test' to disable tcmalloc or unit tests

Sample
----
Please refer to /sample directory

