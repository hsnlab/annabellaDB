# AnnaBellaDB

AnnaBellaDB is a proof-of-concept (PoC) network latency and access-pattern aware key-value store. In our opinion, this solution is ideal to adapt the stateless design and use AnnaBellaDB as a cloud database where the application states could be externalized.

This PoC have been forked from one of the most effective key-value store from the academy, called AnnaDB which was developed in in the [RISE Lab](https://rise.cs.berkeley.edu) at [UC Berkeley](https://berkeley.edu).

More info about AnnaDB can be found at:
* https://github.com/hydro-project/anna
* https://dsf.berkeley.edu/jmh/papers/anna_ieee18.pdf
* https://www.vikrams.io/papers/anna-vldb19.pdf

## Installation

### Installing on your host natively

First of all, you need to install all dependencies of the building process:

```
TODO
```

To build the code, execute the following command in the _/annabellaDB_ directory:
```
bash scripts/build.sh -bRelease -j4
```

The script above is going to generate all binaries into the _build_ directory.

To run a single AnnaBellaDB instance locally, execute in the _/annabellaDB_ directory:
```
./scripts/start-anna-local.sh n y
```
This will open the CLI of the database. Before you are would do anything, please wait at least 20 seconds!! (FIXME: Tackle this BUG).

To write a key/value to the AnnaBellaDB, type the following command in the CLI:
```
PUT <key> <value>
```

Logically, to read the value of a key:
```
GET <key>
```

To STOP AnnaBellaDB, run the following in the _/annabellaDB_ directory of the repository:
```
sudo ./scripts/stop-anna-local.sh y
```


### Installing via Docker
TODO

## Performance measurements of AnnaBellaDB
TODO

## TODOs:


