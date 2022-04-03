FROM docker.io/library/ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y build-essential libsystemd-dev less findutils git libasio-dev autoconf automake

COPY src src

RUN cd /src && g++ -std=c++20 echo_server.cpp -lsystemd -o /socket-activate-echo

FROM docker.io/library/ubuntu:22.04
RUN apt-get update && apt-get install -y curl
COPY --from=builder /socket-activate-echo /socket-activate-echo
CMD ["/socket-activate-echo", "--debug"]
