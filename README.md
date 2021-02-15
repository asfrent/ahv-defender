# Overview


## Links



*   GitHub: [https://github.com/asfrent/ahv-defender](https://github.com/asfrent/ahv-defender)
*   Live Demo: [http://ahv-defender.com/](http://ahv-defender.com/)


## Description

The purpose of this project is to prevent an email server from sending emails that might contain AHV numbers, thus protecting against accidental private data leaks. The main requirements for this system are accuracy of detection, efficiency and security. It consists of five main parts:



*   <strong><code>email-analyzer</code></strong>, a tool that scans over text, extracts AHV numbers and checks them against a database of encrypted entries.
*   <strong><code>lookup-server</code></strong>, a service that maintains a database of hashed AHV numbers. It provides gRPC interface that allows for efficient adding, removing and looking up AHVs.
*   <strong><code>tools</code> </strong>for manipulating AHVs, data generation, command line interaction with the lookup service and more.
*   <strong><code>a web platform</code></strong> that ties all of the above together.
*   <strong><code>configuration files</code> </strong>for Docker and Kubernetes, allowing for deployment on a cluster in minutes.

Note on the web platform and deployment: this is in no way ready for production, it was just a challenge for me to see if I could put everything together - think of it as a simulation that's as close to reality as possible given the short timeframe. All of this was implemented in just 3 days.


## Key Points

The system I implemented has the following notable characteristics:



*   can process data and extract AHVs at a maximum speed of **100MB/s** (see the Paranoid extraction mode).
*   uses **BCrypt** to hash AHVs (see Security Considerations).
*   can serve up to **one billion hashes** with deterministic **lookup times of 2-3ms** (see the Radix Cache).
*   overall lookup times (taking hashing, network into account) are **10ms / RPC** at one billion entries (see Life of a Lookup Query).
*   provides a **set of tools** to manipulate text, AHV lists, manually send queries and more (see Appendx - Tools).


## License

The code is distributed under an MIT license.


# Email Analyzer

The [email analyzer](https://github.com/asfrent/ahv-defender/blob/main/email-analyzer/email-analyzer.cc) reads data from standard input and tries to guess whether it contains AHV numbers (outputs matches to standard output). It does so by using a combination of regular expressions, boolean functions and validation logic. It provides three main modes: standard, thorough and paranoid. See [this file](https://github.com/asfrent/ahv-defender/blob/main/scripts/testdata/ahv-list.txt) (part of a quick and dirty integration test) for some example formats (check out the corresponding matches for [standard](https://github.com/asfrent/ahv-defender/blob/main/scripts/testdata/ea-test-standard.txt), [thorough](https://github.com/asfrent/ahv-defender/blob/main/scripts/testdata/ea-test-thorough.txt) and [paranoid](https://github.com/asfrent/ahv-defender/blob/main/scripts/testdata/ea-test-paranoid.txt) modes).


### Design

The email analyzer tool consists of 3 main extractor classes, one for each mode that share the same [base class](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVExtractor.hpp). The derived classes are responsible for finding possible matches, while the base class is responsible for the validation, recording of matches and deduplication.

The email analyzer can also act as a [gRPC client](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVDatabaseClient.hpp) to the lookup server - if we give it a valid address, it will make lookup calls there and check whether any of the matched AHVs are known to the system.


### AHV Number Validation

The [validation utility function](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVUtil.hpp) employs the following checks:



*   the length of an AHV number is 13
*   it starts with 7 5 6
*   there's only digits, nothing else
*   the control digit is correct


### The standard mode

The [standard mode](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVExtractorStandard.hpp) uses a simple regular expression that matches most commonly seen AHV formats. It allows for spaces, dashes and dots to be used as separators and only matches numbers with 4 digit groups (this being the only boolean condition it checks), as the regex is quite restrictive.


### The thorough mode

The [thorough mode](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVExtractorThorough.hpp) is there to filter out mistakes. The thorough also uses regex to match candidates, but it includes more separators (underscore, colon, semicolon, etc.). These extra characters were chosen either because they are close on the keyboard to the usual separators or because they are on the same key (when using shift). It also allows more digit groups and it even allows separators to appear twice.


### The paranoid mode

Nothing can hide from the [paranoid mode](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVExtractorParanoid.hpp). It scans at 100 MB/s and if there's something to find, it will definitely find it. Here I let my passion for algorithms and optimization to go wild. I devised an algorithm that matches any subsequence of digits in a string with the only condition that they're not too far apart (note that all modes require the AHV to be valid, though, so checksums are still computed). It extracts positions of digits from the original string, computes the gaps between them and then uses a double ended queue to extract matches in O(N), single pass. The double ended queue algorithm is used to limit the maximum gap between digits and it can find AHV numbers even if their digits are hundreds of MB apart. It might be a bit too much, but I love it.


### Side Note

The main purpose of this tool is to detect and prevent possible AHV leaks in emails. It can be integrated with a mail server (eg. by using it as a filter in MTA software such as Exim or Postfix), but it can be also used to perform other interesting tasks:



*   one could use this tool to clean up an inconsistently formatted list of AHVs.
*   if we want to make sure that no AHVs are leaked from our company we can use the paranoid mode to scan for all possible AHVs in a list - better safe than sorry, as false positives are not a problem in this case


### Possible Improvements

I guess looking into real world data is the most important thing we can do here. Since I had no access to such data, I had to go by intuition - this is not the correct way. I would have loved to see a few AHV formats and use that as a starting point.


## 


# Lookup Server

The lookup server acts like a disk backed database with a sophisticated cache in front of it.


### Design

The lookup server implements a gRPC interface with Add, Remove and Lookup methods. The [AHVDiskDatabase](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVDiskDatabase.hpp) class managed two objects - one for disk storage ([AHVStore_File](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVStore_File.hpp)) and one for in memory caching (derived from [AHVCache_Base](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVCache_Base.hpp)). There are two possible implementations for the cache: a deterministic one, [AHVCache_HashMap](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVCache_HashMap.hpp), that stores all the data in RAM and a probabilistic one, [AHVCache_Radix](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVCache_Radix.hpp), that only stores 32 bit hash prefixes. The heavylifting in case of the radix cache is done by the [AHVCache_RadixBucket](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVCache_RadixBucket.hpp) class. A BCrypt C++ wrapper is implemented in [BCryptHasher](https://github.com/asfrent/ahv-defender/blob/main/lib/BCryptHasher.hpp).


### Security Considerations

We cannot store AHV numbers in plaintext in production for security reasons, so we need to encrypt them somehow. I opted for the [BCrypt hasher / KDF](https://www.openwall.com/crypt/) instead of a more popular SHA-xx algorithm because of three main reasons:



*   <strong>rainbow tables</strong> - SHA-xx can be precomputed. This way, an attacker can start early and precompute hashes if we're not careful enough to use a salt. BCrypt asks us to provide randomness in order to generate salts, so there's less room for error.
*   <strong>time tested</strong> - BCrypt has been used for more than 15 years and it became one of the standard ways to store password hashes for user authentication.
*   <strong>cost parameter</strong> - BCrypt uses a configurable "cost" parameter that makes the computation exponentially harder with each increment. The hardware nowadays is specialized in computing SHA-xx and that makes bruteforcing the whole AHV space (which is only 10 billion in size) very appealing. Setting a high parameter enables a company to compute hashes for a cost that will discourage most attackers from even attempting to decrypt the whole database.

Nevertheless, there's no silver bullet here - the main problem is the small AHV space. If we have about 1M AHVs to protect, then an attacker will be able to decrypt on average 1 hash for every ~1K random hashes attempted. This, combined with the fact that most AHVs start with a large number of zeros (which means low entropy) makes the defender's life very complicated.


### Lookup Performance

How quickly can we answer queries and how much memory do we need? I tested on my machine, 4 cores @ 3.9 GHz, 16G RAM.



1. [Hashing](https://github.com/asfrent/ahv-defender/blob/main/lib/BCryptHasher.hpp), by itself:
*   most of the time is spent hashing the AHVs. Even for low cost parameters, the computation takes about 5-6ms. Memory amount is also not negligible by design, but the cost is only temporary.
2. [HashMap based cache](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVCache_HashMap.hpp), by itself (not used int the current configuration, served as a starting point):
*   ~ 1M entries: 1 ms / lookup, ~180M RAM
*   ~ 10M entries: 2 ms / lookup, ~1.6G RAM
*   ~ 100M entries, 5 ms / lookup, ~14G RAM
3. [Radix cache](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVCache_RadixBucket.hpp), by itself:
*   ~ 1M entries: 1ms / lookup ~8M RAM (requested by statement)
*   ~ 10M entries: 1ms / lookup ~80M RAM (population of Switzerland)
*   ~ 100M entries: 1ms / lookup ~800M RAM (population of Europe)
*   <strong>~ 1G entries: 1ms / lookup ~8G RAM (population of China)</strong>
4. [Disk Storage](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVStore_File.hpp):
*   ~1ms / lookup, the caches are so efficient at quickly guessing indexes so that we do on average **1-2 reads of 32 bytes** from SSD.


### Life of a Lookup Query

Let's look a bit into the way a lookup query is processed:



1. the plaintext AHV is received in gRPC Lookup method.
2. we hash the AHV using BCrypt with a predefined salt (5-6ms).
3. the hash is then sent to the database and hits the cache.
4. the radix cache selects one of 256 cache shards based on the first byte and forwards it to it - we're now dealing with a problem 1/256 smaller.
5. we select a prefix of the hash and look for it in 3 places (1ms)
    1. serving area
    2. add delta
    3. remove delta
6. we combine results from these three and return a list of possible indexes in the disk storage.
7. the possible indexes are verified on disk and we can now tell for sure whether we've seen the AHV before (1-2ms).
8. the answer is sent back in the response object and the RPC finishes (1ms)


### The Radix Cache

The [radix cache](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVCache_Radix.hpp) is **sharded 256 ways** and each one of the [shards](https://github.com/asfrent/ahv-defender/blob/main/lib/AHVCache_RadixBucket.hpp) manages a serving area and two deltas.

**Serving area** is made of two very large contiguous blocks of memory that keep 32bit hash prefixes and their indexes in the storage. These blocks are sorted by the prefixes and lookup is done using binary search - we need to randomly access memory only about 32 times until we have a result.

**Add and Rmove deltas** are two sets (binary trees) that keep the latest records that were added / removed, but not yet offloaded into the serving area.

**Compactions **are periodic processes that rebuild the serving areas by applying the deltas to it. At maximum loads these compactions are still very efficient, they take about 2s to complete (1G entries in RAM).


### Bloom Filter

The statement pointed to a probabilistic data structure to solve the efficiency problem and bloom filter came to mind. There's a few drawbacks with this approach so I decided to go wild and write the Radix cache because of the memory requirements, the fact that a bloom filter can only answer probabilistically and the fact that we cannot execute the Remove operation on a bloom filter (which is a bit sad).


### Possible Improvements



*   Implement a database (Mongo, MySQL, etc.) store. Will take more space, but it will be more reliable, easier to maintain and build upon.
*   Use hot swapping when compacting radix shards.
*   Use a distributed hashing service rather than hash in the lookup server.
*   Secure the salt used by BCrypt as much as possible (place the hashing process outside of lookup server).
*   Investigate other key derivation functions and hashing algorithms.
*   Write more tests. Many more tests.


# 


# Web Platform

The web server is implemented in JavaScript using NodeJS / Express. It takes user input, passes it to the email analyzer and checks the results. Depending on whether AHV numbers were detected in text it redirects the user to an appropriate page (Email Sent / Email Filtered). It actually tries to send emails, so please be careful - it might actually succeed, although the SMTP provider is neither reliable, nor reputable. You can check out the server code here: [https://github.com/asfrent/ahv-defender/blob/main/web/index.js](https://github.com/asfrent/ahv-defender/blob/main/web/index.js).

Note that since the email transporter module contains the password to a real email address it was kept out of the git tree.


# Configuration Files for Deployment

I provided basic Docker and Kubernetes configuration files for the lookup server and web service. These are by no means ready to use in production, but helped me quickly spin up the web service that runs a live demo. The frontend runs on a Linode cluster, you can check it out at [http://ahv-defeder.com/](http://ahv-defeder.com/).


## Lookup Server

The lookup server runs on an Ubuntu Docker base image with no additional packages installed (all the necessary libraries have been linked into the main binary). There's one deployment config and one service config for Kubernetes, with the service allowing the email analyzer to address it through DNS rather than IP address.


## Web Server

These configuration files (Docker, Kubernetes deployment and service) are used to surface the whole system to the end user. One notable difference is that the web server service is placed behind a load balancer.


## 


# Appendix - Tools


## 


# cli


### Usage


```
./cli db_server_address add|remove|lookup [quiet|time]
```



### Description

This tool reads AHV entries from standard input and sends the to the lookup server. It normally outputs true / false if the operation is successful, but this output can be suppressed by using the quiet mode.This tool is useful for manual testing and debugging as well as for automated integration testing.

There's also the time mode that shows some performance metrics such as wall time, QPS and average request duration.


### Code

[https://github.com/asfrent/ahv-defender/blob/main/lookup-server/cli.cc](https://github.com/asfrent/ahv-defender/blob/main/lookup-server/cli.cc)


### Example


```
$ ./ahv-gen 1000 | ./cli localhost:12000 add time
true
true
...
true
true
Took 11203ms.
QPS: 90
Average request duration: 11ms.
```



## 


# ahv-gen


### Usage


```
./ahv-gen count
```



### Description

This tool generates a list of random (but valid) AHV numbers. On my machine it generates more than 1M entries in less than one second.


### Code

[https://github.com/asfrent/ahv-defender/blob/main/tools/ahv-gen.cc](https://github.com/asfrent/ahv-defender/blob/main/tools/ahv-gen.cc)


### Example


```
$ time ./ahv-gen 4
7561079469134
7562484929237
7563164048828
7569633978825
```



## 


# db-build


### Usage


```
./db-build
```



### Description

Efficiently builds a database of hashes for the lookup server. Rather than using the cli tool to load data into the server, we can use this tool to prebuild the database offline. It reads AHV numbers from stdin, hashes them on multiple threads and outputs them in the correct format at the standard output. The implementation is rather simple (it is based on the fact that hashing each AHV takes roughly the same amount of time). I was able to generate more than 1M records in about 40 minutes.


### Code

[https://github.com/asfrent/ahv-defender/blob/main/tools/db-build.cc](https://github.com/asfrent/ahv-defender/blob/main/tools/db-build.cc)


### Example


```
$ time ./ahv-gen 1000 | ./db-build > h

real    0m2.256s
user    0m8.045s
sys     0m0.061s
```



## 


# db-gen-fake


### Usage


```
./db-gen-fake count hashes_file
```



### Description

Generates about 3 million fake hashes per second - these do not correspond (or, more precisely, we have no idea whether they do) to AHV numbers, but it's useful for testing the lookup server with large amounts of data. I used it to generate a database of 1 billion entries in about than 5 minutes.


### Code

[https://github.com/asfrent/ahv-defender/blob/main/tools/db-gen-fake.cc](https://github.com/asfrent/ahv-defender/blob/main/tools/db-gen-fake.cc)


### Example

 `$ time ./db-gen-fake 1000000 h`


```
Done.

real    0m0.372s
user    0m0.341s
sys     0m0.024s
```



## 


# db-gen


### Usage


```
./db-gen count plaintext_file hashes_file
```



### Description

Generates two files: plaintext - a list of plaintext AHVs and hashes - a database to be loaded into the lookup, essentially bundling the functionality of ahv-gen and db-build. Multithreaded, uses the same threading model as db-build.


### Code

[https://github.com/asfrent/ahv-defender/blob/main/tools/db-gen.cc](https://github.com/asfrent/ahv-defender/blob/main/tools/db-gen.cc)


### Example


```
$ time ./db-gen 1000 plain hashes
Remaining: 0
Done.
real    0m2.228s
user    0m8.021s
sys     0m0.053s
