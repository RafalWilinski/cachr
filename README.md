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

### Using
You can test solution with following command:
```bash
./test/request.sh
```

or 

```bash
curl localhost:3001/one_second -w %{time_connect}:%{time_starttransfer}:%{time_total} & 
curl localhost:3001/one_second -w %{time_connect}:%{time_starttransfer}:%{time_total} &
```

First request response time should be considerably bigger than subsequent calls' response time.

### Todo
- [x] Add/implement stack (stack will indicate empty positions in `fds` array)
- [x] Make requests to target
- [x] Store request results in dictionary
- [x] CRON-like mechanism for freeing memory in dicitonary
- [x] Returning dict contents if possible
- [x] Tests

### License
[MIT License](https://opensource.org/licenses/MIT) © Marcin Elantkowski, Rafał Wiliński

### Libraries Used
 - [inih](https://github.com/benhoyt/inih)
 - [uthash](https://github.com/troydhanson/uthash)
