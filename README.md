### Cachr [![Build Status](https://travis-ci.org/RafalWilinski/cachr.svg?branch=master)](https://travis-ci.org/RafalWilinski/cachr)

Simple HTTP Caching proxy implementation in pure C.

### Compiling
```
cmake .
make
```

### Running
1. Edit `config.ini` file or create your own
2. Run with `./cachr` command. You can optionally supply own config by passing it as argument to executable. E.g. `./cachr new_config.ini`

### Todo
- [x] Add/implement stack (stack will indicate empty positions in `fds` array)
- [x] Make requests to target
- [x] Store request results in dictionary
- [ ] CRON-like mechanism for freeing memory in dicitonary
- [ ] Returning dict contents if possible
- [ ] Tests
- [x] Dummy target server in Node

##### Nice to have
- [x] Resolve non-blocking issues
- [ ] Better error handling with `strerror`

### License
[MIT License](https://opensource.org/licenses/MIT) © Marcin Elantkowski, Rafał Wiliński

### Libraries Used
 - [inih](https://github.com/benhoyt/inih)
