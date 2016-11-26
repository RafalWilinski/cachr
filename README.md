### Cachr

Simple HTTP Caching proxy implementation in pure C.

### Compiling
```
cmake .
make
```

### Running
1. Edit `config.ini` file or create your own
2. Run with `./cachr` command. You can optionally supply own config by passing it as argument to executable. E.g. `./cachr new_config.ini`

### License
[MIT License](https://opensource.org/licenses/MIT) © Marcin Elantkowski, Rafał Wiliński


### Libraries Used
 - [inih](https://github.com/benhoyt/inih)
