# socket-activate-echo

An echo server that supports systemd socket activation

family   | type        | support
-------  | ----        | -------
AF_UNIX  | SOCK_STREAM | yes
AF_INET  | SOCK_STREAM | yes
AF_INET6 | SOCK_STREAM | yes
AF_UNIX  | SOCK_DGRAM  | yes (but no support for outgoing traffic when run in container because the client socket path on the host is not accessible from within the container) 
AF_INET  | SOCK_DGRAM  | yes
AF_INET6 | SOCK_DGRAM  | yes

### Requirements

* __podman__  version 3.4.0 (released September 2021) or newer
* __container-selinux__ version 2.181.0 (released March 2022) or newer

(If you are using an older version of __container-selinux__ and it does not work, add `--security-opt label=disable` to `podman run`)

### Installation

1. Install __socat__
    ```
    sudo dnf -y install socat
    ```

### About the container image

The container image __ghcr.io/eriksjolund/socket-activate-echo__
is built by the GitHub Actions workflow [../.github/workflows/publish_container_image.yml](../.github/workflows/publish_container_image.yml)
from the file [../Containerfile](../Containerfile).

### Activate an instance of a templated systemd user service

1. Start the echo server sockets
    ```
    git clone https://github.com/eriksjolund/socket-activate-echo.git
    mkdir -p ~/.config/systemd/user
    cp -r socket-activate-echo/systemd/echo* ~/.config/systemd/user
    systemctl --user daemon-reload
    systemctl --user start echo@demo.socket
    ```

2. Run the commands
    ```
    $ echo hello | socat - tcp4:127.0.0.1:3000
    hello
    $ echo hello | socat - tcp6:[::1]:3000
    hello
    $ echo hello | socat - udp4:127.0.0.1:3000
    hello
    $ echo hello | socat - udp6:[::1]:3000
    hello
    $ echo hello | socat - unix:$HOME/echo_stream_sock.demo
    hello
    ```

3. Try establishing an outgoing connection
    ```
    $ podman exec -t echo-demo curl https://podman.io
    curl: (6) Could not resolve host: podman.io
    $
    ```
    (The command-line option `--network=none` was added to prevent the container from establishing outgoing connections)

### Socket activate SOCK_STREAM sockets with systemd-socket-activate

1. Socket activate the echo server
    ```
    systemd-socket-activate -l /tmp/stream.sock -l 4000 podman run --rm --name echo2 --network=none ghcr.io/eriksjolund/socket-activate-echo
    ```

2. In another shell
    ```
    $ echo hello | socat - unix:/tmp/stream.sock
    hello
    $ echo hello | socat - tcp4:127.0.0.1:4000
    hello
    $ echo hello | socat - tcp6:[::1]:4000
    hello
    ```

3. Try establishing an outgoing connection
    ```
    $ podman exec -t echo2 curl https://podman.io
    curl: (6) Could not resolve host: podman.io
    $
    ```

### Socket activate SOCK_DGRAM sockets with systemd-socket-activate

1. Socket activate the echo server
    ```
    systemd-socket-activate --datagram -l 5000 podman run --rm --name echo3 --network=none ghcr.io/eriksjolund/socket-activate-echo
    ```

2. In another shell
    ```
    $ echo hello | socat - udp4:127.0.0.1:5000
    hello
    $ echo hello | socat - udp6:[::1]:5000
    hello
    ```

3. Try establishing an outgoing connection
    ```
    $ podman exec -t echo3 curl https://podman.io
    curl: (6) Could not resolve host: podman.io
    $
    ```

### Troubleshooting

If __socat__ does not receive any reply within a certain time it might terminate before getting the reply. The timeout is __0.5 seconds__ by default.

This could happen if the container image  __ghcr.io/eriksjolund/socket-activate-echo__ needs to be pulled when `podman run` starts. Downloading the container image might take longer than the configured timeout. Symptoms of this could be

```
$ systemctl --user start echo@demo.socket
$ echo hello | socat - udp4:127.0.0.1:3000
$ echo hello | socat - udp4:127.0.0.1:3000
hello
```

To work around the problem, you could for instance pull the image beforehand and add the command-line option `--pull=never` to `podman run`. Another solution is to increase the socat timeout. To configure the timeout to be 30 seconds, add the command-line option `-t 30`.

```
$ echo hello | socat -t 30 - udp4:127.0.0.1:3000
hello
```

A good way to diagnose problems is to look in the journald log for the service:

```
journalctl -xe --user -u echo@demo.service
```
