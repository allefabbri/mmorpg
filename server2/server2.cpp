#define _WIN32_WINNT     0x06010000

#include <iostream>
#include <boost/asio.hpp>
#include <boost/thread.hpp>


class my_connection {
public:
  my_connection(void); // constructor

                       // we must have a new io_service for EVERY THREAD!
  boost::asio::io_service io_service;

  // where we receive the accepted socket and endpoint
  boost::shared_ptr<boost::asio::ip::tcp::socket> socket;
  boost::asio::ip::tcp::endpoint endpoint;

  // keep track of the thread for this connection
  boost::shared_ptr<boost::thread> thread;

  // keep track of the acceptor io_service so we can call stop() on it!
  boost::asio::io_service *master_io_service;

  // boolean to indicate a desire to kill this connection
  bool close;

  // NOTE: you can add other variables here that store connection-specific
  // data, such as received HTML headers, or logged in username, or whatever
  // else you want to keep track of over a connection
};

my_connection::my_connection(void) : close(false) {
  // create new socket into which to receive the new connection
  this->socket = boost::shared_ptr<boost::asio::ip::tcp::socket>(
    new boost::asio::ip::tcp::socket(this->io_service)
    );
}

/**
* helper function
*/
void set_result(
  boost::optional<boost::system::error_code> *destination,
  boost::system::error_code source
  ) {
  destination->reset(source);
}

/**
* helper function
*/
void set_bytes_result(
  boost::optional<boost::system::error_code> *error_destination,
  size_t *transferred_destination,
  boost::system::error_code error_source,
  size_t transferred_source
  ) {
  error_destination->reset(error_source);
  *transferred_destination = transferred_source;
}

/**
* emulate synchronous read with a timeout on socket
*
* returns -1 on error or socket close, 0 on timeout, or bytes received
*/
size_t read_with_timeout(
  boost::asio::ip::tcp::socket &socket,
  void *buf,
  size_t count,
  int seconds
  ) {
  boost::optional<boost::system::error_code> timer_result;
  boost::optional<boost::system::error_code> read_result;
  size_t bytes_transferred;

  // set up a timer on the io_service for this socket
  boost::asio::deadline_timer timer(socket.get_io_service());
  timer.expires_from_now(boost::posix_time::seconds(seconds));
  timer.async_wait(
    boost::bind(
      set_result,
      &timer_result,
      boost::asio::placeholders::error
      )
    );

  // set up asynchronous read (respond when ANY data is received)
  boost::asio::async_read(
    socket,
    boost::asio::buffer((char *)buf, count),
    boost::asio::transfer_at_least(1),
    boost::bind(
      set_bytes_result,
      &read_result,
      &bytes_transferred,
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred
      )
    );

  socket.get_io_service().reset();

  // set default result to zero (timeout) because another thread
  // may call io_service().stop() deliberately to interrupt the
  // the read (say, if it wanted to signal the thread that there
  // was data to write)
  size_t result = 0;
  bool resultset = false;
  while (socket.get_io_service().run_one()) {
    if (read_result) {
      //boost::system::error_code e = (*read_result);
      //std::cerr << "read_result was " << e.message() << std::endl;
      timer.cancel();
      if (resultset == false) {
        result = (bytes_transferred <= 0) ? -1 : bytes_transferred;
        resultset = true;
      }
      read_result.reset();
    }
    else if (timer_result) {
      socket.cancel();
      if (resultset == false) {
        result = 0;
        resultset = 0;
      }
      timer_result.reset();
    }
  }

  return(result);
}

size_t write_with_timeout(
  boost::asio::ip::tcp::socket &socket,
  void const *buf,
  size_t count,
  int seconds
  ) {
  boost::optional<boost::system::error_code> timer_result;
  boost::optional<boost::system::error_code> write_result;
  size_t bytes_transferred;

  boost::asio::deadline_timer timer(socket.get_io_service());
  timer.expires_from_now(boost::posix_time::seconds(seconds));
  timer.async_wait(
    boost::bind(
      set_result,
      &timer_result,
      boost::asio::placeholders::error
      )
    );

  boost::asio::async_write(
    socket,
    boost::asio::buffer((char *)buf, count),
    boost::asio::transfer_at_least(count), // want to transfer ALL of it
    boost::bind(
      set_bytes_result,
      &write_result,
      &bytes_transferred,
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred
      )
    );

  socket.get_io_service().reset();

  size_t result = -1;
  bool resultset = false;
  while (socket.get_io_service().run_one()) {
    if (write_result) {
      timer.cancel();
      if (resultset == false) {
        result = (bytes_transferred <= 0) ? -1 : bytes_transferred;
        resultset = true;
      }
      write_result.reset();
    }
    else if (timer_result) {
      socket.cancel();
      if (resultset == false) {
        result = 0;
        resultset = true;
      }
      timer_result.reset();
    }
  }
  return(result);
}


void process_line(my_connection connection, std::string line) {
  return;
}

void worker(
  boost::shared_ptr<my_connection> connection
  ) {
  boost::asio::ip::tcp::socket &socket = *(connection->socket);
  boost::asio::socket_base::non_blocking_io make_non_blocking(true);
  socket.io_control(make_non_blocking);

  char acBuffer[1024];
  std::string line("");

  while (connection->close == false) {
    size_t bytes_read = read_with_timeout(
      socket, // socket to read
      acBuffer, // buffer to read into
      sizeof(acBuffer), // maximum size of buffer
      1 // timeout in seconds
      );

    if (bytes_read < 0)
      break; // connection error or close

    if (bytes_read == 0) {
      continue; // timeout

      char const *pend = acBuffer + bytes_read;
      char const *pstart = acBuffer;
      char const *pchar = pstart;
      // buffer may legitimately contain '\0' from network
      // so we must always ensure we don't go over the number
      // of bytes actually read
      while ((pchar < pend) && (*pchar != '\0')) {
        if ((*pchar != '\n') && (*pchar != '\r')) {
          pchar++;
          continue;
        }

        // non-blank line detected?
        if (pchar > pstart) {
          line += std::string(pstart, pchar - pstart);
          // ***THIS IS WHAT WE ULTIMATELY WANTED TO ACHIEVE!!!***
//          process_line(*connection, line);
          line = "";
        }

        // skip over newlines
        while ((pchar < pend) && ((*pchar == '\n') || (*pchar == '\r')))
          pchar++;

        pstart = pchar;
        continue;
      }

      if (pchar > pstart) {
        // put remaining non-terminated text into line buffer
        line += std::string(pstart, pchar - pstart);
      }
    } // while connection not to be closed
  }
}

class my_server {
public:
  my_server(
    boost::asio::io_service *io_service,
    const boost::asio::ip::tcp::endpoint &endpoint
    );

  void handle_accept(const boost::system::error_code& error);

  bool failed;

private:
  boost::asio::io_service *io_service;
  boost::asio::ip::tcp::endpoint endpoint;
  boost::asio::ip::tcp::acceptor *acceptor;
  boost::shared_ptr<my_connection> connection;
};

my_server::my_server(
  boost::asio::io_service *io_service,
  const boost::asio::ip::tcp::endpoint &endpoint
  ) {
  this->io_service = io_service;
  this->failed = false; // indicator whether construction failed

                        // it is a common problem to find that the port we bind to
                        // is already in use (say, another instance of this program)
  try {
    this->acceptor = new boost::asio::ip::tcp::acceptor(*io_service);

    // Open the acceptor with the option to reuse the address
    // (i.e. SO_REUSEADDR)
    this->acceptor->open(endpoint.protocol());
    this->acceptor->set_option(
      boost::asio::ip::tcp::acceptor::reuse_address(true)
      );
    this->acceptor->bind(endpoint);
    this->acceptor->listen();
  }
  catch (boost::system::system_error e) {
    std::cerr << "Error binding to " << endpoint.address().to_string() << ":" << endpoint.port() << ": " << e.what() << std::endl;
    this->failed = true;
    return;
  }

  // successful bind!
  // Now create a new "my_connection" object to receive new accepted socket
  this->connection = boost::shared_ptr<my_connection>(
    new my_connection()
    );
  this->connection->master_io_service = this->io_service;
  this->acceptor->async_accept(
    *(this->connection->socket), // new connection is stored here
    this->connection->endpoint, // where the remote address is stored
    boost::bind(
      &my_server::handle_accept, // function to call on accept()
      this, // object functions need a pointer to their object
      boost::asio::placeholders::error // argument to call-back function
      )
    );
}

void my_server::handle_accept(
  const boost::system::error_code& error
  ) {
  if (error) {
    // accept failed
    std::cerr << "Acceptor failed: " << error.message() << std::endl;
    return;
  }

  std::cout << "Accepted connection from " << this->connection->endpoint.address().to_string() << ":" << this->connection->endpoint.port() << std::endl;

  // time to create a thread and let THAT deal with the socket synchronously!
  this->connection->thread = boost::shared_ptr<boost::thread>(
    new boost::thread(worker, this->connection)
    );

  // re-build accept call
  // we need a new socket/connection class
  this->connection = boost::shared_ptr<my_connection>(
    new my_connection()
    );
  this->connection->master_io_service = this->io_service;
  this->acceptor->async_accept(
    *(this->connection->socket),
    this->connection->endpoint,
    boost::bind(
      &my_server::handle_accept,
      this,
      boost::asio::placeholders::error
      )
    );
}


/**
* hostname to ip address string (using DNS lookup)
*/
std::string get_ip_address(
  boost::asio::io_service &io_service,
  std::string hostname
  ) {
  boost::asio::ip::tcp::resolver resolver(io_service);
  boost::asio::ip::tcp::resolver::query query(hostname, "80"); // any port number will do
  boost::asio::ip::tcp::resolver::iterator iterator;
  try {
    iterator = resolver.resolve(query);
  }
  catch (boost::system::system_error e) {
  std::cerr << "Error resolving " << hostname << ": " << e.what() << std::endl;
    return("");
  }

  boost::asio::ip::tcp::resolver::iterator end;
  if (iterator == end)
    return("");

  return(iterator->endpoint().address().to_string());
}

/**
* main I/O loop
*  sets up the listening address(es) and runs I/O asynchronous service
*/
int do_input_output(
  std::list< std::pair<std::string, unsigned int> > listeners
  ) {
  // create I/O service
  boost::asio::io_service io_service;

  // start a server for each listen address
  std::list< boost::shared_ptr<my_server> > servers; // track in a list
  for (
    std::list< std::pair<std::string, unsigned int> >::iterator it = listeners.begin();
    it != listeners.end();
    it++
    ) {
    std::string hostname = it->first;
    unsigned int port = it->second;

    // endpoint to assign to
    boost::asio::ip::tcp::endpoint endpoint;
    if (!hostname.empty()) {
      std::string address = get_ip_address(io_service, hostname);
      if (address.empty()) {
        std::cerr << "could not resolve " << hostname << std::endl;
        return(1);
      }

      endpoint = boost::asio::ip::tcp::endpoint(
        boost::asio::ip::address::from_string(address),
        port
        );
    }
    else {
      endpoint = boost::asio::ip::tcp::endpoint(
        boost::asio::ip::tcp::v4(),
        port
        );
    }

    // create server
    boost::shared_ptr<my_server> server(
      new my_server(&io_service, endpoint)
      );

    if (server->failed) {
      std::cerr << "Failure in creatig server" << std::endl;
      return(1);
    }
    servers.push_back(server);

    std::cout << "listen on \"" << endpoint << "\"" << std::endl;
  } // for each listener

    // now start the I/O service
    // can only stop by calling io_service.stop()
  io_service.run();

  return(0); // everything went okay
}


int main() {

  return 0;
}