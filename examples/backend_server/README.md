# Example backend server

`simple_backend.py` is a throwaway Python HTTP/1.1 server used to exercise the proxy
locally. It is **not** part of AegisProxy; it only needs to exist so that the three of
us can run a realistic topology before any C++ backend exists.

```bash
python3 examples/backend_server/simple_backend.py --port 9001 --name app-1 &
python3 examples/backend_server/simple_backend.py --port 9002 --name app-2 --delay-ms 50 &
./build/aegisproxy --config config/aegisproxy.example.conf
curl -i http://127.0.0.1:8080/hello      # scaffold answers 501 until forwarding exists
```

`scripts/run-local.sh` does the above for you.
