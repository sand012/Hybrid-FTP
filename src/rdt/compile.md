# run  test

## run first terminal

```basch
g++ -std=c++17 -Wall -Wextra UDPSocket.cpp udp_server_test.cpp -o udp_server_test

g++ -std=c++17 -Wall -Wextra UDPSocket.cpp udp_client_test.cpp -o udp_client_test

./udp_server_test

```

## open second terminal

```basch
./udp_client_test
```
