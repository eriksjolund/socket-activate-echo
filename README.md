# socket-activate-echo

An echo server that supports systemd socket activation

family   | type        | support
-------  | ----        | -------
AF_UNIX  | SOCK_STREAM | yes
AF_INET  | SOCK_STREAM | yes
AF_INET6 | SOCK_STREAM | yes
AF_VSOCK | SOCK_STREAM | yes
AF_UNIX  | SOCK_DGRAM  | yes (but no support for outgoing traffic when run in container because the client socket path on the host is not accessible from within the container) 
AF_INET  | SOCK_DGRAM  | yes
AF_INET6 | SOCK_DGRAM  | yes

### Requirements

* __podman__  version 3.4.0 (released September 2021) or newer
* __container-selinux__ version 2.183.0 (released April 2022) or newer

If you are using an older version of __container-selinux__ and it does not work, add `--security-opt label=disable` to `podman run`.

### Installation

1. Install __socat__
    ```
    sudo dnf -y install socat
    ```

### About the container image

The container image [__ghcr.io/eriksjolund/socket-activate-echo__](https://github.com/eriksjolund/socket-activate-echo/pkgs/container/socket-activate-echo)
is built by the GitHub Actions workflow [.github/workflows/publish_container_image.yml](.github/workflows/publish_container_image.yml)
from the file [./Containerfile](./Containerfile).

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
    $ echo hello | socat - VSOCK-CONNECT:1:3000
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
    systemd-socket-activate -l /tmp/stream.sock \
        -l 4000 -l vsock:4294967295:4000 podman run --rm --name echo2 \
        --network=none ghcr.io/eriksjolund/socket-activate-echo
    ```
    Instead of _VMADDR_CID_ANY_ (4294967295) we could also have used _VMADDR_CID_LOCAL_ (1), in other words,
    `-l vsock:1:4000` (see [`man 7 vsock`](https://man7.org/linux/man-pages/man7/vsock.7.html)).

2. In another shell
    ```
    $ echo hello | socat - unix:/tmp/stream.sock
    hello
    $ echo hello | socat - tcp4:127.0.0.1:4000
    hello
    $ echo hello | socat - tcp6:[::1]:4000
    hello
    $ echo hello | socat - VSOCK-CONNECT:1:4000
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
    systemd-socket-activate --datagram \
        -l 5000 podman run --rm --name echo3 \
        --network=none ghcr.io/eriksjolund/socket-activate-echo
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

### Run the echo container inside a VM and connect over AF_VSOCK (SOCK_STREAM)

1. Install requirements

    ```
    sudo dnf install -y qemu butane coreos-installer
    ```

2.  Start the Fedora CoreOS VM by running these commands on the host

    ```
    STREAM=next
    CID=20
    mkdir -p ~/.local/share/libvirt/images/
    file=$(coreos-installer download -s "${STREAM}" -p qemu -f qcow2.xz --decompress -C ~/.local/share/libvirt/images/)
    cat vm/echo.butane | butane --strict --pretty --files-dir systemd > file.ign
    qemu-kvm -m 2048 \
      -device "vhost-vsock-pci,id=vhost-vsock-pci0,guest-cid=$CID" \
      -cpu host -nographic -snapshot \
      -drive "if=virtio,file=$file" \
      -fw_cfg name=opt/com.coreos/config,file=file.ign -nic "user,model=virtio"
    ```

    The Context Identifier (CID) is an arbitrary number that is used to identify the VM (see `man vsock`).

3.  Run on the host
    ```
     $ CID=20
     $ echo hello | socat -t 30 - VSOCK-CONNECT:$CID:3000
     hello
    ```

### Troubleshooting

#### The container takes long time to start

Pulling a container image may take long time. This delay can be avoided by pulling the container
image beforehand and adding the command-line option `--pull=never` to `podman run`.

#### socat times out before receiving the reply

If __socat__ does not receive any reply within a certain time limit it terminates before getting the reply. The timeout is __0.5 seconds__ by default.
Symptoms of this could be

```
$ systemctl --user start echo@demo.socket
$ echo hello | socat - udp4:127.0.0.1:3000
$ echo hello | socat - udp4:127.0.0.1:3000
hello
```

To configure the timeout to be 30 seconds, add the command-line option `-t 30`.

```
$ echo hello | socat -t 30 - udp4:127.0.0.1:3000
hello
```

Another way to handle the problem is to use the command-line option __readline__ to get an interactive user interface. Type the word _hello_  and see it being echoed back. 

```
$ socat readline udp4:127.0.0.1:3000
hello
hello
```

In this case there will be no timeout because none of the channels have reached EOF.

A good way to diagnose problems is to look in the journald log for the service:

```
journalctl -xe --user -u echo@demo.service
```
