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
#include <charconv>
#include <optional>
#include <ranges>
#include <vector>

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

void log_handled_bytes(int num_bytes, std::string handled, std::optional< std::string > fdname, bool logdebug)
{
      if (logdebug)
      {
        std::cout << "Bytes " << handled << " = " << num_bytes;
        if (fdname.has_value()) {
          std::cout << ", File descriptor name = \"" << fdname.value() << "\"";
        }
        std::cout << std::endl;
      }
}

template<typename T >
awaitable<void> echo(T socket, bool logdebug, std::optional< std::string > fdname)
{
  try
  {
    char data[1024];
    for (;;)
    {
      std::size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
      log_handled_bytes(n, "received", fdname, logdebug);
      co_await async_write(socket, asio::buffer(data, n), use_awaitable);
      log_handled_bytes(n, "sent", fdname, logdebug);
    }
  }
  catch (std::exception& e)
  {
    // std::printf("echo Exception: %s\n", e.what());
  }
}

awaitable<void> listener(int fd, bool logdebug, std::optional< std::string > fdname)
{
  auto executor = co_await this_coro::executor;
  static const asio::local::stream_protocol prot;
  if (sd_is_socket(fd, AF_UNIX, SOCK_STREAM, 1)) {
    asio::basic_socket_acceptor acceptor{executor, prot, fd};
    for (;;)
    {
      asio::local::stream_protocol::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, echo<asio::local::stream_protocol::socket>(std::move(socket), logdebug, fdname), detached);
    }
  }
  if (sd_is_socket(fd, AF_VSOCK, SOCK_STREAM, 1)) {
    asio::basic_socket_acceptor acceptor{executor, prot, fd};
    for (;;)
    {
      asio::local::stream_protocol::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, echo<asio::local::stream_protocol::socket>(std::move(socket), logdebug, fdname), detached);
    }
  }
  if (sd_is_socket(fd, AF_INET, SOCK_STREAM, 1)) {
    asio::basic_socket_acceptor acceptor{executor, asio::ip::tcp::v4(), fd};
    for (;;)
    {
      tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, echo<tcp::socket>(std::move(socket), logdebug, fdname), detached);
    }
  }
  if (sd_is_socket(fd, AF_INET6, SOCK_STREAM, 1)) {
    asio::basic_socket_acceptor acceptor{executor, asio::ip::tcp::v6(), fd};
    for (;;)
    {
      tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
      co_spawn(executor, echo<tcp::socket>(std::move(socket), logdebug, fdname), detached);
    }
  }
}

#include "./async_udp_echo_server.cpp"

struct ParsedArgs
{
  bool debug;
  bool sdnotify;
  unsigned int start_sleep;
};

ParsedArgs parse_argv(int argc, char *argv[])
{
  bool debug = false;
  bool sdnotify = false;
  unsigned int start_sleep = 0;
  bool error_found = false;
  if (argc == 0) {
    error_found = true;
  }
  { int i = 1;
    while (i < argc) {
    if (std::string_view(argv[i]) == "--start-sleep" || std::string_view(argv[i]) == "-t") {
      if (i + 1 < argc) {
	std::string_view const str(argv[i + 1]);
	auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), start_sleep);
        if (ec != std::errc{}) {
          throw std::runtime_error("--start-sleep value is not an unsigned integer");
	}
	++i;
      } else {
        throw std::runtime_error("--start-sleep expects a value");
      }
    } else
    if (std::string_view(argv[i]) == "--debug" || std::string_view(argv[i]) == "-d") {
      debug = true;
    } else
    if (std::string_view(argv[i]) == "--sdnotify" || std::string_view(argv[i]) == "-s") {
      sdnotify = true;
    } else {
      error_found = true;
      break;
    }
    ++i;
    }}
  if (error_found) {
    throw std::runtime_error("Incorrect command-line arguments. See --help for usage");
  }
  return ParsedArgs{debug, sdnotify, start_sleep};
}

void print_usage(FILE *f)
{
  fprintf(f, "Usage: socket-activate-echo [OPTION]\n"
             "-h, --help                        print help\n"
             "-d, --debug                       print debug output to stderr\n"
             "-s, --sdnotify                    enable sd_notify\n"
	     "-t, --start-sleep seconds         add start sleep (unsigned int)\n\n"
             "To use the echo server functionality, let this program be started by a systemd service or systemd-socket-activate.\n");
}

void log_sd_notify(std::string msg, bool sdnotify, bool logdebug) {
  if (sdnotify) {
    sd_notify(0, msg.c_str());
    if (logdebug) {
      std::cout << "sd_notify(0,\"" << msg << "\");" << std::endl;
    }
  }
}

struct listen_fds_with_names {
  public:
  std::optional< std::string > get_fdname(int index) const {
    if (index < fdnames.size()) {
      return  fdnames[index];
    }
    return {};
  }
  const int num_sd_listen_fds;
  const std::vector< std::string > fdnames;
};

const listen_fds_with_names parse_sd_listen_fds_with_names() {
   char** names = NULL;
   int num_sd_listen_fds = sd_listen_fds_with_names(1, &names);
   std::vector< std::string > fdnames;
   if (names) {
     int i = 0;
     while (names[i]) {
       fdnames.push_back(std::string{names[i]});
       ++i;
     }
     free(names);
   }
  return listen_fds_with_names{num_sd_listen_fds, fdnames};
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

    const listen_fds_with_names listen_info = parse_sd_listen_fds_with_names();

    int num_fds = sd_listen_fds(1);
    if (listen_info.num_sd_listen_fds < 1) {
      throw std::runtime_error("This program needs to be started by a systemd service or by systemd-socket-activate. (LISTEN_FDS is not correctly set)");
    }

    asio::io_context io_context(1);

    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto){ io_context.stop(); });

    std::vector < std::unique_ptr < server < asio::ip::udp > > > udp_servers;
    static const asio::local::datagram_protocol datagram_prot;
    std::vector < std::unique_ptr < server < asio::local::datagram_protocol > > > unix_dgram_servers;
    std::vector < std::unique_ptr < server < asio::local::datagram_protocol > > > vsock_dgram_servers;

    for (int i = 0; i < listen_info.num_sd_listen_fds; i++) {
      auto fdname = listen_info.get_fdname(i);
      int fd = SD_LISTEN_FDS_START + i;
      if (sd_is_socket(fd, AF_UNSPEC, SOCK_STREAM, 1)) {
	co_spawn(io_context, listener(fd, parsed_args.debug, fdname), detached);
      } else
      if (sd_is_socket(fd, AF_UNIX, SOCK_DGRAM, -1)) {
        unix_dgram_servers.push_back(std::make_unique< server< asio::local::datagram_protocol > >(io_context,datagram_prot, parsed_args.debug, fdname, fd));
      } else
      if (sd_is_socket(fd, AF_VSOCK, SOCK_DGRAM, -1)) {
        vsock_dgram_servers.push_back(std::make_unique< server< asio::local::datagram_protocol > >(io_context,datagram_prot, parsed_args.debug, fdname, fd));
      } else
      if (sd_is_socket(fd, AF_INET, SOCK_DGRAM, -1)) {
        udp_servers.push_back(std::make_unique< server< asio::ip::udp > >(io_context,asio::ip::udp::v4(), parsed_args.debug, fdname, fd));
      } else
      if (sd_is_socket(fd, AF_INET6, SOCK_DGRAM, -1)) {
	udp_servers.push_back(std::make_unique< server < asio::ip::udp > >(io_context,asio::ip::udp::v6(), parsed_args.debug, fdname, fd));
      }
    }
    if (parsed_args.start_sleep) {
      const std::string msg = std::string("Sleeping ") + std::to_string(parsed_args.start_sleep) +
                              " second(s) as specified by the --start-sleep (-t) option";
      if (parsed_args.debug) {
        std::cout << msg << std::endl;
      }
      log_sd_notify("STATUS=" + msg, parsed_args.sdnotify, parsed_args.debug);
      sleep(parsed_args.start_sleep);
    }
    log_sd_notify("READY=1", parsed_args.sdnotify, parsed_args.debug);
    log_sd_notify ("STATUS=Server is ready", parsed_args.sdnotify, parsed_args.debug);
    io_context.run();
  }
  catch (std::exception& e)
  {
    std::fprintf(stderr, "Error: %s\n", e.what());
    exit(EXIT_FAILURE);
  }
  return EXIT_SUCCESS;
}
