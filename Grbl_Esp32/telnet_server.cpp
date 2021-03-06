/*
  telnet_server.cpp -  telnet server functions class

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifdef ARDUINO_ARCH_ESP32

#include "config.h"

#if defined (ENABLE_WIFI) &&  defined (ENABLE_TELNET)

#include "wifiservices.h"

#include "grbl.h"

#include "telnet_server.h"
#include "wificonfig.h"
#include <WiFi.h>
#include <Preferences.h>
#include "report.h"


Telnet_Server telnet_server;
bool Telnet_Server::_setupdone = false;
uint16_t Telnet_Server::_port = 0;
WiFiServer * Telnet_Server::_telnetserver = NULL;
WiFiClient Telnet_Server::_telnetClients[MAX_TLNT_CLIENTS];

Telnet_Server::Telnet_Server(){
    _RXbufferSize = 0;
    _RXbufferpos = 0;
}
Telnet_Server::~Telnet_Server(){
    end();
}


bool Telnet_Server::begin(){
   
    bool no_error = true;
    _setupdone = false;
    Preferences prefs;
    _RXbufferSize = 0;
    _RXbufferpos = 0;;
    prefs.begin(NAMESPACE, true);
    int8_t penabled = prefs.getChar(TELNET_ENABLE_ENTRY, DEFAULT_TELNET_STATE);
    //Get telnet port
    _port = prefs.getUShort(TELNET_PORT_ENTRY, DEFAULT_TELNETSERVER_PORT);
    prefs.end();
    
    if (penabled == 0) return false;
    //create instance
    _telnetserver= new WiFiServer(_port);
    _telnetserver->setNoDelay(true);
    String s = "[MSG:TELNET Started " + String(_port) + "]\r\n";
    grbl_send(CLIENT_ALL,(char *)s.c_str());
    //start telnet server
    _telnetserver->begin();
    _setupdone = true;
    return no_error;
}

void Telnet_Server::end(){
    _setupdone = false;
    _RXbufferSize = 0;
    _RXbufferpos = 0;
    if (_telnetserver) {
        delete _telnetserver;
        _telnetserver = NULL;
    }
}

void Telnet_Server::clearClients(){
     //check if there are any new clients
    if (_telnetserver->hasClient()){
      uint8_t i;
      for(i = 0; i < MAX_TLNT_CLIENTS; i++){
        //find free/disconnected spot
        if (!_telnetClients[i] || !_telnetClients[i].connected()){
          if(_telnetClients[i]) _telnetClients[i].stop();
          _telnetClients[i] = _telnetserver->available();
          break;
        }
      }
      if (i >= MAX_TLNT_CLIENTS) {
        //no free/disconnected spot so reject
        _telnetserver->available().stop();
      }
    }
}

size_t Telnet_Server::write(const uint8_t *buffer, size_t size){
    
    if ( !_setupdone || _telnetserver == NULL) {
        log_i("[TELNET out blocked]");
        return 0;
        }
    clearClients();
    log_i("[TELNET out]");
    //push UART data to all connected telnet clients
    for(uint8_t i = 0; i < MAX_TLNT_CLIENTS; i++){
        if (_telnetClients[i] && _telnetClients[i].connected()){
            log_i("[TELNET out connected]");
          _telnetClients[i].write(buffer, size);
          wifi_config.wait(0);
        }
    }
}

void Telnet_Server::handle(){
    //check if can read
    if ( !_setupdone || _telnetserver == NULL) {
        return;
        }
    clearClients();
    //check clients for data
    uint8_t c;
    for(uint8_t i = 0; i < MAX_TLNT_CLIENTS; i++){
      if (_telnetClients[i] && _telnetClients[i].connected()){
        if(_telnetClients[i].available()){
          //get data from the telnet client and push it to grbl
          while(_telnetClients[i].available() && (available() < TELNETRXBUFFERSIZE)) {
              wifi_config.wait(0);
              c = _telnetClients[i].read();
              if ((char)c != '\r')push(c);
              if ((char)c == '\n')return;
          }
        }
      }
      else {
        if (_telnetClients[i]) {
          _telnetClients[i].stop();
        }
      }
       wifi_config.wait(0);
    }
}

int Telnet_Server::peek(void){
    if (_RXbufferSize > 0)return _RXbuffer[_RXbufferpos];
    else return -1;
}

int Telnet_Server::available(){
    return _RXbufferSize;
}

bool Telnet_Server::push (uint8_t data){
    log_i("[TELNET]push %c",data);
    if ((1 + _RXbufferSize) <= TELNETRXBUFFERSIZE){
        int current = _RXbufferpos + _RXbufferSize;
        if (current > TELNETRXBUFFERSIZE) current = current - TELNETRXBUFFERSIZE;
        if (current > (TELNETRXBUFFERSIZE-1)) current = 0;
        _RXbuffer[current] = data;
        _RXbufferSize++;
        log_i("[TELNET]buffer size %d",_RXbufferSize);
       return true;
    }
    return false;
}

bool Telnet_Server::push (const char * data){
    int data_size = strlen(data);
    if ((data_size + _RXbufferSize) <= TELNETRXBUFFERSIZE){
        int current = _RXbufferpos + _RXbufferSize;
        if (current > TELNETRXBUFFERSIZE) current = current - TELNETRXBUFFERSIZE;
        for (int i = 0; i < data_size; i++){
        if (current > (TELNETRXBUFFERSIZE-1)) current = 0;
        _RXbuffer[current] = data[i];
        current ++;
        wifi_config.wait(0);
        }
        _RXbufferSize+=strlen(data);
        return true;
    }
    return false;
}

int Telnet_Server::read(void){
    
    if (_RXbufferSize > 0) {
        int v = _RXbuffer[_RXbufferpos];
        log_i("[TELNET]read %c",v);
        _RXbufferpos++;
        if (_RXbufferpos > (TELNETRXBUFFERSIZE-1))_RXbufferpos = 0;
        _RXbufferSize--;
        return v;
    } else return -1;
}

#endif // Enable TELNET && ENABLE_WIFI

#endif // ARDUINO_ARCH_ESP32
