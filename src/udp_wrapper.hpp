// client

#define _WIN32_WINNT 0x0A00

#include <iostream>
#include <iomanip>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>

#define SEPARATOR         ":"

using boost::asio::ip::udp;

// BOOST UDP WRAPPER
class UDPConnection {
private:
  boost::asio::io_service& IoService;
  udp::socket Socket;

  boost::asio::deadline_timer dl_timer;

  void check_deadline() {
    if (dl_timer.expires_at() <= boost::asio::deadline_timer::traits_type::now()) {
      Socket.cancel();
      dl_timer.expires_at(boost::posix_time::pos_infin);
    }
    dl_timer.async_wait(boost::bind(&UDPConnection::check_deadline, this));
  }

public:
  udp::endpoint EP_recv, EP_send;
  size_t len_recv, len_send;

  // constructors
  UDPConnection(boost::asio::io_service& io_service, const std::string& host_recv, const std::string& port_recv, const std::string& host_send, const std::string& port_send)
    : IoService(io_service),
    Socket(io_service, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)),
    dl_timer(io_service) {
  
    udp::resolver resolver(IoService);
    udp::resolver::query query_recv(udp::v4(), host_recv, port_recv);
    EP_recv = *resolver.resolve(query_recv);
    udp::resolver::query query_send(udp::v4(), host_send, port_send);
    EP_send = *resolver.resolve(query_send);
    Socket = udp::socket(io_service, EP_recv);

    dl_timer.expires_at(boost::posix_time::pos_infin);
    check_deadline();
  }

  UDPConnection(boost::asio::io_service& io_service, const std::string& host_recv, const std::string& port_recv)
    : IoService(io_service),
    Socket(io_service, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)),
    dl_timer(io_service) {
    
    udp::resolver resolver(IoService);
    udp::resolver::query query_recv(udp::v4(), host_recv, port_recv);
    EP_recv = *resolver.resolve(query_recv);
    Socket = udp::socket(io_service, EP_recv);

    dl_timer.expires_at(boost::posix_time::pos_infin);
    check_deadline();
  }

  ~UDPConnection() {
    Socket.close();
  }

  // sending methods overload
  void send(const std::string& msg, boost::asio::ip::udp::endpoint ep) {
    len_send = Socket.send_to(boost::asio::buffer(msg, msg.size()), ep);
  }

  void send(const std::string& msg) {
    len_send = Socket.send_to(boost::asio::buffer(msg, msg.size()), EP_send);
  }

  // receiving methods overload
  void recv(const boost::asio::mutable_buffers_1 & buffer, boost::system::error_code & err) {
    len_recv = Socket.receive(buffer, 0, err);
  }

  std::string recv(boost::system::error_code & err) {
    std::array<char, 128> buffer;
    len_recv = Socket.receive(boost::asio::buffer(&buffer[0], buffer.size()), 0, err);
    return std::string(buffer.data(), len_recv);
  }

  void recv_timer(const boost::asio::mutable_buffer& buffer,
    boost::posix_time::time_duration timeout, boost::system::error_code& ec) {

    dl_timer.expires_from_now(timeout);

    ec = boost::asio::error::would_block;
    std::size_t length = 0;

    Socket.async_receive(boost::asio::buffer(buffer),
      boost::bind(&UDPConnection::handle_receive, _1, _2, &ec, &length));

    // Block until the asynchronous operation has completed.
    do IoService.run_one(); while (ec == boost::asio::error::would_block);

    len_recv = length;
  }

  // async timer methods
  static void handle_receive(
    const boost::system::error_code& ec, std::size_t length,
    boost::system::error_code* out_ec, std::size_t* out_length)
  {
    *out_ec = ec;
    *out_length = length;
  }

  // info methods
  friend std::ostream& operator<<(std::ostream& os, const UDPConnection& udp_c) {
    os
      << "|-- Connection data " << std::setw(36) << std::setfill('-') << "|" << std::endl
//      << std::left << std::setw(18) << "| Sending to "
//      << std::right << std::setw(15) << udp_c.EP_send.address() << ":"
//      << std::left << std::setw(6) << udp_c.EP_send.port() << std::right << std::setw(8) << "|" << std::endl
      << std::left << std::setw(18) << std::setfill(' ') << "| Socket info    "
      << std::right << std::setw(15) << udp_c.EP_recv.address() << ":"
      << std::left << std::setw(6) << udp_c.EP_recv.port() << std::right << std::setw(8) << "|" << std::endl
      << "|" << std::setw(55) << std::setfill('-') << "|" << std::endl;

    return os;
  }
};


// BOOST related auxiliary functions
boost::asio::ip::udp::endpoint make_boost_endpoint(boost::asio::io_service& ioService, std::string address, std::string port) {
  udp::resolver resolver(ioService);
  udp::resolver::query q(boost::asio::ip::udp::v4(), address, port);
  udp::endpoint ep = *resolver.resolve(q);
  return ep;
}


// CLIENT CLASSES
class Client_data {
public:
  std::string name;
  int id;
  udp::endpoint ep;

  Client_data(){}
  Client_data(boost::asio::io_service& ioService, std::string s) { // s = "name:id:address:port"
    std::vector<std::string> tokens;
    boost::algorithm::split(tokens, s, boost::algorithm::is_any_of(SEPARATOR), boost::algorithm::token_compress_on);
    name = tokens[0];
    id = std::stoi(tokens[1]);
    udp::resolver resolver(ioService);
    udp::resolver::query q(boost::asio::ip::udp::v4(), tokens[2], tokens[3]);
    ep = *resolver.resolve(q);
  }
};

class Match_data {
public:
  Client_data c1, c2;
  udp::endpoint match_ep;

  Match_data(boost::asio::io_service& ioService, Client_data C1, Client_data C2, std::string host, std::string port){
    c1.name = C1.name; c1.id = C1.id; c1.ep = C1.ep;
    c2.name = C2.name; c2.id = C2.id; c2.ep = C2.ep;
    boost::asio::ip::udp::resolver resolver(ioService);
    boost::asio::ip::udp::resolver::query q(boost::asio::ip::udp::v4(), host, port);
    match_ep = *resolver.resolve(q);
  }
};
