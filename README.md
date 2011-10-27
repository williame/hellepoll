***Hellepoll*** is a simple asynchronous web-server written in C++.

It is available under the standard Simplified BSD License; see the LICENSE file for details.

Speed
-----

You only write this kind of code if you are concerned about speed :)

<table border=1>
<tr><td>benchmark<th>Tornado (Python)<th>node.js<th>deft (Java)<th>Hellepoll
<tr align=right><th>ab -c25<td>2K<td>9K<td>13K<td>18K/s
<tr align=right><th>ab -k -c25<td>7K<td>?<td>30K<td>40K/s+
</table>

(At 40K/sec on my laptop I am running out of memory bandwidth, and my CPU is not maxed out yet.  Numbers ought to be higher on normal server-like hardware.)

Where to start?
---------------

Look at helloworld.cpp.  Its mostly about parsing commandline arguments, but in there is a simple class that prints "Hello world" on every request.  Can't start simplier than that!

Status
------

It is very much alpha code, mostly for those wanting to build their own inner guts and just want code to glance at.

It would be nice to imagine it growing into a proper 'tornado for C++' but that's a long way off.

Helloworld is an example simple HTTP server that is included, and used in speed benchmarking.

Architecture
------------

It is built around an asynchronous IO loop, which is the scheduler class; currently this is epoll, but in prinicple it could be kqueue or equivilent.  It can use edge-triggering, and if used it will check that you sate the stream in your slice.

You would have one instance of the scheduler class for each thread.  Ideally, your server is single threaded so you don't have to worry about locking and such, just run a separate process instance for each core that you have.

In the IO loop are any number of tasks - half a million is not so scary.

Protocol handlers - such as HTTP - use state machines to track their progress.

Todo
----

- HTTP server should have a handler factory instead, based on the address and the registration of regexes or stems or something
- For non-chunked keep-alive replies, all writes should be buffered so the content-length can be computed by the framework
- Profile and improve speed of inner loop
- Integrate async file IO too
- Profile and improve speed e.g. play with ```writev()```
- ```scheduler.add_callback()``` and general helpers for writing async programs
- A templating system for HTML
- Errors reported to task handlers
- More built-in handlers e.g. async memcached, redis, even SQL somehow

