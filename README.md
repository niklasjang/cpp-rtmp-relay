# cpp-rtmp-relay
RTMP relay server in cpp

## branch : simple

```
$ git clone
$ cd src
$ gcc simpleServer.c -o simpleServer
$ ./simpleServer 9190
$ gcc simpleClient.c -o simpleClient
$ ./simpleClient 127.0.0.1 9190
```

```
//Output
$ Message from server: Hello World!
```