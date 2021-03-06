/*
 * ubirch#1 M66 Modem core functionality interface.
 *
 * @author Niranjan Rao
 * @date 2017-02-09
 *
 * @copyright &copy; 2015, 2016, 2017 ubirch GmbH (https://ubirch.com)
 *
 * ```
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ```
 */

#include <string.h>
#include <fsl_rtc.h>
#include "M66Interface.h"

// Various timeouts for different M66 operations
#define M66_CONNECT_TIMEOUT 15000
#define M66_SEND_TIMEOUT    15000
#define M66_RECV_TIMEOUT    40000
#define M66_MISC_TIMEOUT    40000

// M66Interface implementation
M66Interface::M66Interface(PinName tx, PinName rx, PinName rstPin, PinName pwrPin)
    : _m66(tx, rx, rstPin, pwrPin), _sockets(), _apn(), _userName(), _passPhrase(), _imei(), _cbs()
{
    memset(_sockets, 0, sizeof(_sockets));
    memset(_cbs, 0, sizeof(_cbs));

    _m66.attach(this, &M66Interface::event);
}

bool M66Interface::powerUpModem(){
    return _m66.startup();
}

bool M66Interface::reset() {
    return _m66.reset();
}

bool M66Interface::powerDown(){
    return _m66.powerDown();
}

bool M66Interface::isModemAlive() {
    return _m66.isModemAlive();
}

int M66Interface::checkGPRS() {
    return _m66.checkGPRS();
}

int M66Interface::set_imei(){
    if(!_m66.getIMEI(_imei)){
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    return NSAPI_ERROR_OK;
}

const char *M66Interface::get_imei(){
    return _imei;
}

nsapi_error_t M66Interface::connect(const char *sim_pin, const char *apn, const char *uname, const char *pwd) {
    set_sim_pin(sim_pin);
    set_credentials(apn, uname, pwd);
    return connect();
}

int M66Interface::connect()
{
    _m66.setTimeout(M66_CONNECT_TIMEOUT);

    if (!_m66.startup()) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    if (!_m66.connect(_apn, _userName, _passPhrase)) {
        return NSAPI_ERROR_NO_CONNECTION;
    }

    if (!_m66.getIPAddress()) {
        return NSAPI_ERROR_NO_ADDRESS;
    }

    if(set_imei()){
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    return NSAPI_ERROR_OK;
}

void M66Interface::set_credentials(const char *apn, const char *userName, const char *passPhrase)
{
    memset(_apn, 0, sizeof(_apn));
    strncpy(_apn, apn, sizeof(_apn));

    memset(_userName, 0, sizeof(_userName));
    strncpy(_userName, userName, sizeof(_userName));

    memset(_passPhrase, 0, sizeof(_passPhrase));
    strncpy(_passPhrase, passPhrase, sizeof(_passPhrase));
}

int M66Interface::disconnect()
{
    _m66.setTimeout(M66_MISC_TIMEOUT);

    if (!_m66.disconnect()) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    return NSAPI_ERROR_OK;
}

const char *M66Interface::get_ip_address()
{
    return _m66.getIPAddress();
}

bool M66Interface::get_location(char *lon, char *lat) {
    return _m66.getLocation(lon, lat);
}

bool M66Interface::getDateTime(tm *dateTime, int *zone) {
    return _m66.getDateTime(dateTime, zone);
}

bool M66Interface::getUnixTime(time_t *t) {
    return _m66.getUnixTime(t);
}

bool M66Interface::queryIP(const char *url, const char *theIP){
    return _m66.queryIP(url, theIP);
}

bool M66Interface::getModemBattery(uint8_t *status, int *level, int *voltage){
    return _m66.modem_battery(status, level, voltage);
}

struct m66_socket {
    int id;
    nsapi_protocol_t proto;
    bool connected;
    SocketAddress addr;
};

nsapi_error_t M66Interface::gethostbyname(const char *host, SocketAddress *address, nsapi_version_t version) {

    if (address->set_ip_address(host)) {
        if (version != NSAPI_UNSPEC && address->get_ip_version() != version) {
            return NSAPI_ERROR_DNS_FAILURE;
        }

        return NSAPI_ERROR_OK;
    }

    char *ipbuff = new char[NSAPI_IP_SIZE];
    int ret = 0;

    if(!_m66.queryIP(host, ipbuff)) {
        ret = NSAPI_ERROR_DEVICE_ERROR;
    } else {
        address->set_ip_address(ipbuff);
    }

    delete[] ipbuff;
    return ret;
}

int M66Interface::socket_open(void **handle, nsapi_protocol_t proto)
{
    // Look for an unused socket
    int id = -1;

    for (int i = 0; i < M66_SOCKET_COUNT; i++) {
        if (!_sockets[i]) {
            id = i;
            _sockets[i] = true;
            break;
        }
    }

    if (id == -1) {
        return NSAPI_ERROR_NO_SOCKET;
    }

    struct m66_socket *socket = new struct m66_socket;
    if (!socket) {
        return NSAPI_ERROR_NO_SOCKET;
    }

    socket->id = id;
    socket->proto = proto;
    socket->connected = false;
    *handle = socket;
    return 0;
}

int M66Interface::socket_close(void *handle)
{
    struct m66_socket *socket = (struct m66_socket *)handle;
    int err = 0;
    _m66.setTimeout(M66_MISC_TIMEOUT);

    if (!_m66.close(socket->id)) {
        err = NSAPI_ERROR_DEVICE_ERROR;
    }

    _sockets[socket->id] = false;
    delete socket;
    return err;
}

int M66Interface::socket_bind(void *handle, const SocketAddress &address)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

int M66Interface::socket_listen(void *handle, int backlog)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

int M66Interface::socket_connect(void *handle, const SocketAddress &addr)
{
    struct m66_socket *socket = (struct m66_socket *)handle;
    _m66.setTimeout(M66_MISC_TIMEOUT);

    const char *proto = (socket->proto == NSAPI_UDP) ? "UDP" : "TCP";
    if (!_m66.open(proto, socket->id, addr.get_ip_address(), addr.get_port())) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    socket->connected = true;
    return 0;
}

int M66Interface::socket_accept(void *server, void **socket, SocketAddress *addr)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

int M66Interface::socket_send(void *handle, const void *data, unsigned size)
{
    struct m66_socket *socket = (struct m66_socket *)handle;
    _m66.setTimeout(M66_SEND_TIMEOUT);

    if (!_m66.send(socket->id, data, size)) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    return size;
}

int M66Interface::socket_recv(void *handle, void *data, unsigned size)
{
    struct m66_socket *socket = (struct m66_socket *)handle;
    _m66.setTimeout(M66_RECV_TIMEOUT);

    int32_t recv = _m66.recv(socket->id, data, size);
    if (recv < 0) {
        return NSAPI_ERROR_WOULD_BLOCK;
    }

    return recv;
}

int M66Interface::socket_sendto(void *handle, const SocketAddress &addr, const void *data, unsigned size)
{
    struct m66_socket *socket = (struct m66_socket *)handle;

    if (socket->connected && socket->addr != addr) {
        _m66.setTimeout(M66_MISC_TIMEOUT);
        if (!_m66.close(socket->id)) {
            return NSAPI_ERROR_DEVICE_ERROR;
        }
        socket->connected = false;
    }

    if (!socket->connected) {
        int err = socket_connect(socket, addr);
        if (err < 0) {
            return err;
        }
        socket->addr = addr;
    }

    return socket_send(socket, data, size);
}

int M66Interface::socket_recvfrom(void *handle, SocketAddress *addr, void *data, unsigned size)
{
    struct m66_socket *socket = (struct m66_socket *)handle;
    int ret = socket_recv(socket, data, size);
    if (ret >= 0 && addr) {
        *addr = socket->addr;
    }

    return ret;
}

void M66Interface::socket_attach(void *handle, void (*callback)(void *), void *data)
{
    struct m66_socket *socket = (struct m66_socket *)handle;
    _cbs[socket->id].callback = callback;
    _cbs[socket->id].data = data;
}

void M66Interface::event() {
    for (int i = 0; i < M66_SOCKET_COUNT; i++) {
        if (_cbs[i].callback) {
            _cbs[i].callback(_cbs[i].data);
        }
    }
}

void M66Interface::set_sim_pin(const char *sim_pin) {

}

bool M66Interface::is_connected() {
    return _m66.isConnected();
}

const char *M66Interface::get_netmask() {
    return NULL;
}

const char *M66Interface::get_gateway() {
    return NULL;
}

const char *M66Interface::get_iccid() {
    if (!(_m66.tx("AT+QCCID") && _m66.scan("%22s", _iccid))) {
        return NULL;
    }
    return _iccid;
}
