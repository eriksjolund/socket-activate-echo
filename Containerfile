FROM docker.io/library/ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y build-essential libsystemd-dev less findutils git libasio-dev autoconf automake

COPY src src

RUN cd /src && g++ -std=c++20 echo_server.cpp -lsystemd -o /socket-activate-echo

FROM docker.io/library/ubuntu:22.04
# The packages curl and iproute2 are just installed to provide
# easy access to the commands /usr/bin/curl and /usr/sbin/ip
# for demonstration purposes of --network=none
# Those packages are not required for the functionality of
# the echo server.

RUN apt-get update && apt-get install -y curl iproute2
COPY --from=builder /socket-activate-echo /socket-activate-echo
# 65534:65534 is "nobody:nogroup" in Ubuntu. The systemd project recommends using "nobody:nobody"
# (see https://systemd.io/UIDS-GIDS/).
# Let's use the numeric UID / GID, to avoid being dependent on the stability of the group name.
USER 65534:65534
CMD ["/socket-activate-echo", "--debug"]
