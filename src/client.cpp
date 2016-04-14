/* Copyright 2016 - Alessandro Fabbri */

/***************************************************************************
This file is part of mmorpg.

mmorpg is free software : you can redistribute it and / or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

mmorpg is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with mmorpg. If not, see <http://www.gnu.org/licenses/>.
***************************************************************************/

// GAME CLIENT

#define _SCL_SECURE_NO_WARNINGS

#include "udp_wrapper.hpp"
#include <conio.h>

// GAME SPECIFIC STUFF

#define ACTION_CONTINUE    'c'
#define ACTION_STOP        'q'

class Match_status {
public:
  bool is_active;
  char my_pick, opp_pick;

  Match_status() : is_active(true) {}

  void check_status() {
    if (my_pick == ACTION_STOP && opp_pick == ACTION_STOP) is_active = false;
    else is_active = true;
    return;
  }

  void take_turn() {
    my_pick = '0';
    while (my_pick != ACTION_CONTINUE && my_pick != ACTION_STOP) {
      std::cout << "What to do? [C]ontinue or [Q]uit : ";
      my_pick = _getche();
      if (my_pick == 'C') my_pick = ACTION_CONTINUE;
      if (my_pick == 'Q') my_pick = ACTION_STOP;
      std::cout << std::endl;
    }
    return;
  }

  void opponent_turn(std::string action) {
    opp_pick = action[0];
    return;
  }

};

/////////////////////////////////////////////////////////////////////////////
void usage(char * progname) {
  std::cout << "Usage:    " << progname << " <int>" << std::endl;
}

int main(int argc, char ** argv) {
  if (argc < 2) {
    usage(argv[0]);
    exit(2);
  }

  boost::asio::io_service ioService;
  UDPConnection udp_con(ioService, "localhost", "4000" + std::string(argv[1]));
  boost::system::error_code err;

  // hardcoded server data
  udp::endpoint server_ep = make_boost_endpoint(ioService, "localhost", "40000");

  // Sending connection data
  std::cout << udp_con << std::endl;
  std::string name, id, address, port;
  name = "Client#" + std::string(argv[1]);
  id = "199" + std::string(argv[1]);
  address = udp_con.EP_recv.address().to_string();
  port = std::to_string(udp_con.EP_recv.port());

  std::string msg_handshake = name + SEPARATOR + id + SEPARATOR + address + SEPARATOR + port;
  std::cout << "Handshake message = " << msg_handshake << std::endl;
  udp_con.send(msg_handshake, server_ep);

  // Waiting server ACK
  std::string msg_ack = udp_con.recv(err);
  if (msg_ack == "CONNECTIONOK!") {
    std::cout << "Connection with SERVER established" << std::endl
      << "Awaiting MATCHMAKER..." << std::endl;
  }
  else {
    std::cout << "Connection with SERVER refused. Closing client." << std::endl;
    exit(3);
  }

  // receiving and setting match data
  std::string msg_matchmaking = udp_con.recv(err);    // "Match found!:address:port"

  std::cout << "Matchmaking -> " << msg_matchmaking << std::endl;
  std::vector<std::string> tokens;
  boost::algorithm::split(tokens, msg_matchmaking, boost::algorithm::is_any_of(":"), boost::algorithm::token_compress_on);
  boost::asio::ip::udp::endpoint match_ep = make_boost_endpoint(ioService, tokens[1], tokens[2]);

  // main match loop (turn based)
  std::string msg_start = udp_con.recv(err);    // "opponent_name:your_turn"
  boost::algorithm::split(tokens, msg_start, boost::algorithm::is_any_of(":"), boost::algorithm::token_compress_on);
  bool yourTurn = ((tokens[1] == "yes") ? true : false);

  Match_status match_status;
  std::string msg_match;
  std::cout << "MATCH FOUND! Your opponent is : " << tokens[0] << std::endl;
  while (match_status.is_active) {
    std::cout << ((yourTurn) ? "Your turn" : "Wait for opponent turn to finish") << std::endl;
    if (yourTurn) {
      match_status.take_turn();
      yourTurn = false;
      udp_con.send(name + SEPARATOR + "turn_completed" + SEPARATOR + match_status.my_pick , match_ep);
    }
    else {
      msg_match = udp_con.recv(err);        // "your_turn:opponent_pick"
      boost::algorithm::split(tokens, msg_match, boost::algorithm::is_any_of(":"), boost::algorithm::token_compress_on);
      std::cout << "Opponent's pick : " << tokens[1] << std::endl;
      if (tokens[0] == "your_turn") yourTurn = true;
      match_status.opponent_turn(tokens[1]);
    }
    match_status.check_status();
  }

  udp_con.send(name + SEPARATOR + "match_completed", match_ep);
  std::cout << "GAME OVER" << std::endl;

  return 0;
}

