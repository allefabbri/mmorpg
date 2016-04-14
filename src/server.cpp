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

// GAME SERVER

#define _SCL_SECURE_NO_WARNINGS

#include "udp_wrapper.hpp"

#include <thread>
#include <mutex>
#include <fstream>
#include <conio.h>

using boost::asio::ip::udp;

boost::asio::io_service ioService;
udp::resolver resolver(ioService);
std::vector<udp::endpoint> clients_ep;
std::vector<Client_data> connected_clients;
std::mutex mtx;
std::vector<Match_data> matches;
std::vector<std::thread> matches_t;
boost::posix_time::time_duration timeout(boost::posix_time::milliseconds(100));

void handshake(bool * keepRunning) {
  std::ofstream handshake_log("handshake_log.txt");
  UDPConnection udp_handshake(ioService, "localhost", "40000");
  handshake_log << "SERVER UDP PARAMETERS" << std::endl << udp_handshake << std::endl;

  std::array<char, 128> buffer;
  boost::system::error_code err;

  while (*keepRunning) {
    // receive the handshake message
    try {
      udp_handshake.recv_timer(boost::asio::buffer(&buffer[0], buffer.size()), timeout, err);
    }
    catch (std::exception & e) {
      std::cout << std::endl << e.what() << std::endl;
      std::cout << err.value() << " - " << err.message() << std::endl;
    }


    // timeout behaviour switch
    if (err.value()) {
      //handshake_log << "Handshake socket timed out " << err.value() << std::endl;
    }
    else {
      std::string msg_handshake(buffer.data(), udp_handshake.len_recv);
      handshake_log << std::endl << "Handshake connection established with " << msg_handshake << std::endl;

      // extract the client params and store them
      Client_data c(ioService, msg_handshake);
      handshake_log << "Connection established with client : " << msg_handshake << std::endl;
      mtx.lock();
      connected_clients.push_back(c);
      mtx.unlock();

      // send ACK back
      udp_handshake.send("CONNECTIONOK!", connected_clients.back().ep);

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  return;
}

void matchmaking(bool * keepRunning) {
  int port_index = 1;
  UDPConnection udp_matchmaking(ioService, "localhost", "39998");

  while (*keepRunning) {
    if (connected_clients.size() > 1) {
      std::string match_port = "5000" + std::to_string(port_index);
      Match_data match(ioService, connected_clients[0], connected_clients[1], "localhost", match_port);
      std::string msg_matchmaking = std::string("Match found!") + ":" + "localhost" + ":" + match_port;
      udp_matchmaking.send(msg_matchmaking, match.c1.ep);
      udp_matchmaking.send(msg_matchmaking, match.c2.ep);

      mtx.lock();
      matches.push_back(match);
      connected_clients.erase(connected_clients.begin(), connected_clients.begin() + 2);
      mtx.unlock();

      port_index++;
    }
    else {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  return;
}

void play_match(Match_data match) {
  std::cout << std::endl << "Match started : " << match.c1.name << " VS " << match.c2.name << std::endl;
  UDPConnection udp_match(ioService, match.match_ep.address().to_string(), std::to_string(match.match_ep.port()));
  boost::system::error_code err;

  // roll for starting player
  Client_data *active_player, *waiting_player;
  if (rand() < RAND_MAX / 2) {
    active_player = &(match.c1);
    waiting_player = &(match.c2);
  }
  else {
    active_player = &(match.c2);
    waiting_player = &(match.c1);
  }

  // starting messages     "opponent_name:your_turn"
  udp_match.send(waiting_player->name + SEPARATOR + "yes", active_player->ep);
  udp_match.send(active_player->name + SEPARATOR + "no", waiting_player->ep);

  // main match loop
  std::vector<std::string> tokens;
  std::string msg_active, msg_waiting;
  int match_status = 0;
  while (true) {
    // managing active player
    msg_active = udp_match.recv(err);           // name:turn_completed:pick
    boost::algorithm::split(tokens, msg_active, boost::algorithm::is_any_of(":"), boost::algorithm::token_compress_on);
    if (tokens[1] == "turn_completed") msg_waiting = std::string("your_turn") + SEPARATOR + tokens[2];
    else if (tokens[1] == "match_completed") {
      msg_waiting = std::string("game_over");
      match_status++;
      if (match_status == 2) break;
    }

    // managing waiting player
    if (match_status == 0) udp_match.send(msg_waiting, waiting_player->ep);

    // swapping players
    Client_data * temp_player = active_player;
    active_player = waiting_player;
    waiting_player = temp_player;
  }

  return;
}

void matchstarter(bool * keepRunning) {
  while (*keepRunning) {
    if (matches.size() > 0) {
      mtx.lock();
      matches_t.push_back(std::thread(play_match, matches.front()));
      matches.erase(matches.begin(), matches.begin() + 1);
      mtx.unlock();
    }
    else {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }

  mtx.lock();
  if (matches_t.size()) for (auto & match_t : matches_t) match_t.join();
  mtx.unlock();

  return;
}

void print_info(bool * keepRunning) {
  while (*keepRunning) {
    mtx.lock();
    std::cout << "Connected clients (" << connected_clients.size() << ")  Match created (" << matches.size() << ")  Match playing (" << matches_t.size() << ")\r";
    mtx.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return;
}

void control(bool * keepRunning) {
  char cmd;
  while (*keepRunning) {
    cmd = _getch();
    switch (cmd) {
    case 'q':
    case 'Q':
      mtx.lock();
      *keepRunning = false;
      mtx.unlock();
      break;
    default:
      break;
    }
  }

  return;
}

///////////////////////////////////////////////////////////////////////
int main() {
  std::cout << "Server RUNNING, press 'q' to quit." << std::endl;

  bool keepRunning = true;
  std::thread handshake_t(handshake, &keepRunning);
  std::thread matchmaking_t(matchmaking, &keepRunning);
  std::thread matchstarter_t(matchstarter, &keepRunning);

  std::thread print_info_t(print_info, &keepRunning);
  std::thread control_t(control, &keepRunning);


  // closing operations
  handshake_t.join();
  matchmaking_t.join();
  matchstarter_t.join();
  print_info_t.join();
  control_t.join();
  std::cout << std::endl << "Server shutting down..." << std::endl;
  return 0;
}
