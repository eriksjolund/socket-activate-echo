// This file was first copied from
// https://github.com/chriskohlhoff/asio/blob/bba12d10501418fd3789ce01c9f86a77d37df7ed/asio/src/examples/cpp14/echo/async_udp_echo_server.cpp
// and later modified by Erik Sjölund (erik.sjolund@gmail.com)

//
// async_udp_echo_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <iostream>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>


template <typename PROTOCOL>
class server
{
public:
  server(asio::io_context& io_context, const PROTOCOL &prot, bool logdebug, int fd)
    : socket_{io_context, prot, fd}, logdebug_{logdebug}
  {
    do_receive();
  }
  server(asio::io_context& io_context, bool logdebug, int fd)
    : socket_{io_context, fd}, logdebug_{logdebug}
  {
    do_receive();
  }

  void do_receive()
  {
    socket_.async_receive_from(
        asio::buffer(data_, max_length), sender_endpoint_,
        [this](std::error_code ec, std::size_t bytes_recvd)
        {
	  if (logdebug_)
	  {
	    std::cerr << "Bytes received = " << bytes_recvd << "\n";
	  }
          if (ec)
          {
            std::cerr << "Error while receiving. Error message = " << ec.message() << "\n";
          }
          if (!ec && bytes_recvd > 0)
          {
            do_send(bytes_recvd);
          }
          else
          {
            do_receive();
          }
        });
  }

  void do_send(std::size_t length)
  {
    socket_.async_send_to(
        asio::buffer(data_, length), sender_endpoint_,
        [this](std::error_code ec, std::size_t bytes_sent)
        {
	  if (logdebug_)
	  {
	    std::cerr << "Bytes sent = " << bytes_sent << "\n";
	  }
          if (ec)
          {
            std::cerr << "Error while sending. Error message = " << ec.message() << "\n";
          }
          do_receive();
        });
  }

private:
  const bool logdebug_;
  PROTOCOL::socket socket_;
  PROTOCOL::endpoint sender_endpoint_;
  enum { max_length = 1024 };
  char data_[max_length];
};
