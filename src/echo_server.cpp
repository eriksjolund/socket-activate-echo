// This file was first copied from
// https://github.com/chriskohlhoff/asio/blob/bba12d10501418fd3789ce01c9f86a77d37df7ed/asio/src/examples/cpp17/coroutines_ts/echo_server.cpp
// and later modified by Erik Sjölund (erik.sjolund@gmail.com)

//
// echo_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <systemd/sd-daemon.h>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>
#include <asio/awaitable.hpp>
#include <asio/local/stream_protocol.hpp>
#include <asio/local/stream_protocol.hpp>
#include <asio/local/datagram_protocol.hpp>
#include <cstdio>
#include <iostream>

using asio::ip::tcp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
namespace this_coro = asio::this_coro;

#if defined(ASIO_ENABLE_HANDLER_TRACKING)
# define use_awaitable \
  asio::use_awaitable_t(__FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

template<typename T >
awaitable<void> echo(T socket, bool logdebug)
{
  try
  {
    char data[1024];
    for (;;)
    {
      std::size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
      if (logdebug)
      {
        std::cout << "Bytes received = " << n << std::endl;
      }
      co_await async_write(socket, asio::buffer(data, n), use_awaitable);
      if (logdebug)
      {
        std::cout << "Bytes sent = " << n << std::endl;
      }
    }
  }
  catch (std::exception& e)
  {
    // std::printf("echo Exception: %s\n", e.what());
  }
}

awaitable<void> listener(int fd, bool logdebug)
{
  auto executor = co_await this_coro::executor;
  static const asio::local::stream_protocol prot;
  if (sd_is_socket(fd, AF_UNIX, SOCK_STREAM, 1)) {
    asio::basic_socket_acceptor acceptor{executor, prot, fd};
    for (;;)
    {
      asio::local::stream_protocol::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, echo<asio::local::stream_protocol::socket>(std::move(socket), logdebug), detached);
    }
  }
  if (sd_is_socket(fd, AF_VSOCK, SOCK_STREAM, 1)) {
    asio::basic_socket_acceptor acceptor{executor, prot, fd};
    for (;;)
    {
      asio::local::stream_protocol::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, echo<asio::local::stream_protocol::socket>(std::move(socket), logdebug), detached);
    }
  }
  if (sd_is_socket(fd, AF_INET, SOCK_STREAM, 1)) {
    asio::basic_socket_acceptor acceptor{executor, asio::ip::tcp::v4(), fd};
    for (;;)
    {
      tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, echo<tcp::socket>(std::move(socket), logdebug), detached);
    }
  }
  if (sd_is_socket(fd, AF_INET6, SOCK_STREAM, 1)) {
    asio::basic_socket_acceptor acceptor{executor, asio::ip::tcp::v6(), fd};
    for (;;)
    {
      tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, echo<tcp::socket>(std::move(socket), logdebug), detached);
    }
  }
}

#include "./async_udp_echo_server.cpp"

struct ParsedArgs
{
  bool debug;
  bool sdnotify;
};

ParsedArgs parse_argv(int argc, char *argv[])
{
  bool debug = false;
  bool sdnotify = false;

  bool error_found = false;
  if (argc == 0) {
    error_found = true;
  }
  for (int i = 1; i != argc; ++i) {
    if (std::string_view(argv[i]) == "--debug" || std::string_view(argv[i]) == "-d") {
      debug = true;
    } else
    if (std::string_view(argv[i]) == "--sdnotify" || std::string_view(argv[i]) == "-s") {
      sdnotify = true;
    } else {
      error_found = true;
      break;
    }
  }
  if (error_found) {
    throw std::runtime_error("Incorrect command-line arguments. See --help for usage");
  }
  return ParsedArgs{debug, sdnotify};
}

void print_usage(FILE *f)
{
  fprintf(f, "Usage: socket-activate-echo [OPTION]\n"
             "-h, --help             print help\n"
             "-d, --debug            enable verbose debug output\n"
             "-s, --sdnotify         enable sd_notify\n\n"
             "To use the echo server functionality, let this program be started by a systemd service or systemd-socket-activate.\n");
}

int main(int argc, char *argv[])
{
  if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
    print_usage(stdout);
    exit(EXIT_SUCCESS);
  }

  try
  {
    const ParsedArgs parsed_args = parse_argv(argc, argv);

    int num_fds = sd_listen_fds(1);
    if (num_fds < 1) {
      throw std::runtime_error("This program needs to be started by a systemd service or by systemd-socket-activate. (LISTEN_FDS is not correctly set)");
    }

    asio::io_context io_context(1);

    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto){ io_context.stop(); });

    std::vector < std::unique_ptr < server < asio::ip::udp > > > udp_servers;
    static const asio::local::datagram_protocol datagram_prot;
    std::vector < std::unique_ptr < server < asio::local::datagram_protocol > > > unix_dgram_servers;
    std::vector < std::unique_ptr < server < asio::local::datagram_protocol > > > vsock_dgram_servers;

    for (int i = 0; i < num_fds; i++) {
      int fd = SD_LISTEN_FDS_START + i;
      if (sd_is_socket(fd, AF_UNSPEC, SOCK_STREAM, 1)) {
	co_spawn(io_context, listener(fd, parsed_args.debug), detached);
      } else
      if (sd_is_socket(fd, AF_UNIX, SOCK_DGRAM, -1)) {
        unix_dgram_servers.push_back(std::make_unique< server< asio::local::datagram_protocol > >(io_context,datagram_prot, parsed_args.debug, fd));
      } else
      if (sd_is_socket(fd, AF_VSOCK, SOCK_DGRAM, -1)) {
        vsock_dgram_servers.push_back(std::make_unique< server< asio::local::datagram_protocol > >(io_context,datagram_prot, parsed_args.debug, fd));
      } else
      if (sd_is_socket(fd, AF_INET, SOCK_DGRAM, -1)) {
        udp_servers.push_back(std::make_unique< server< asio::ip::udp > >(io_context,asio::ip::udp::v4(), parsed_args.debug, fd));
      } else
      if (sd_is_socket(fd, AF_INET6, SOCK_DGRAM, -1)) {
	udp_servers.push_back(std::make_unique< server < asio::ip::udp > >(io_context,asio::ip::udp::v6(), parsed_args.debug, fd));
      }
    }
    if (parsed_args.sdnotify) {
      sd_notify(0, "READY=1");
    }
    io_context.run();
  }
  catch (std::exception& e)
  {
    std::fprintf(stderr, "Error: %s\n", e.what());
    exit(EXIT_FAILURE);
  }
  return EXIT_SUCCESS;
}
