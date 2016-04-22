![alt text](doc/neat_logo.png "NEAT LOGO")

**A New, Evolutive API and Transport-Layer Architecture for the Internet**

NEAT supports **FreeBSD**, **Linux**, **OS X** and **NetBSD**


## NEAT internals :nut_and_bolt:
NEAT is a callback based library and everything revovles around the neat_ctx
struct. This struct has to be initialized before anything else can be done. NEAT
uses libuv as event library, and this loop is available to users so that they
can hook in and add their own callbacks.

One of the first things done in NEAT after the library has been initialized is
to start monitoring the available addresses on all interfaces of the machine.
NEAT supports multi-homing and we must therefore have an up-to-date view of the
available (and connected) network resources available on a machine. Address
events are published using an internal event API, which users also can hook
into. It is useful if only a small subset of NEAT is wanted, for example
monitoring the preferred lifetime of a v6 address. Look at neat_resolver.c for
an example on how to use this API. An address is stored in a
platform-independent structure.

After NEAT has been initialized, it is up to the user to do what he or she
wants. A typical first step is to resolve a domain name.

## Requirements :point_up:
* `cmake`
* `libuv`
* `ldns`
* `libmnl (linux only)`

| OS        | command           |
| :-------- |:-------------|
| Ubuntu    | `apt-get install cmake libuv1-dev libldns-dev libmnl-dev` |
| FreeBSD   | `pkg install cmake ldns libuv`     |   
| OS X      | `brew install libuv ldns`      |   

## Quickstart :rocket:
```shell
$ cd <path-to-neat-src>
$ mkdir build && cd build
$ cmake ..
$ cmake --build .
```
This will generate makefiles and compile the library and the samples.
You will find the shared and the static library in the `build` directory and the samples in `build/samples` directory.

In order to install the neat library, simply run - don't forget to run ldconfig after installing neat the first time.
```shell
$ sudo make install
```

For an easy entry to NEAT, have a look at `neat_http_get.c` for a simple http client.

## Read the docs :bulb:
Have a look at our [documentation](doc/README.md)!

## Buildbots :fire:
We are running [buildbots](http://buildbot.nplab.de:28010/waterfall) to support our continuous integration process.

If you are only interested in a single branch, just add `?branch=BRANCHNAME` to the URL. http://buildbot.nplab.de:28010/waterfall?branch=master


## Acknowledgement
* [www.neat-project.org](https://www.neat-project.org)
* [twitter.com/H2020Neat](https://twitter.com/H2020Neat)

This work has received funding from the European Union's Horizon 2020 research and innovation programme under grant agreement No. 644334 (NEAT). The views expressed are solely those of the author(s).
