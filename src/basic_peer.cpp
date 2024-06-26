#include <basic_peer.hpp>
#include <stun.hpp>

#include <thread>

BasicPeer::BasicPeer(ConfigManager& config_manager)
{
    _socket = std::make_shared<QUdpSocket>();
    _socket->bind(QHostAddress("0.0.0.0"), config_manager.get_port());

    _peer_info.nickname = config_manager.get_nickname();
    auto stun_servers = config_manager.get_stun_servers();
    _peer_info.address = stun::get_address(_socket, stun_servers.at(0));
    _peer_info.nat_type = stun::get_nat_type(stun_servers);

    connect(_socket.get(), &QUdpSocket::readyRead, this, &BasicPeer::read_data);
}

void BasicPeer::send_data(QByteArray const& buf, HostAddress addr)
{
    const auto [ip, port] = addr;
    _socket->writeDatagram(buf, QHostAddress(ip.c_str()), port);
    _msg_storage.add_message(_active_peers.key(addr), _active_peers.key(addr), buf.toStdString());
}

void BasicPeer::make_holepunch(HostAddress address, bool brute_enable)
{
    const auto data = "\x11\x11\x11\x11" + _peer_info.nickname;
    if (brute_enable) {
        const auto [ip, port] = address;
        const size_t min_port = (port > 5'000) ? port - 5'000 : 0;
        const size_t max_port = (port < 60'535) ? port + 5'000 : 65'535;
        for (size_t i = min_port; i <= max_port; i++) {
            if (i != _peer_info.address.second) {
                HostAddress addr = std::make_pair(ip, i);
                const auto [ip, port] = address;
                _socket->writeDatagram(QByteArray(data.c_str()), QHostAddress(ip.c_str()), port);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    } else {
        const auto [ip, port] = address;
        _socket->writeDatagram(QByteArray(data.c_str()), QHostAddress(ip.c_str()), port);
    }
}

void BasicPeer::read_data()
{
    QByteArray buf;
    buf.resize(_socket->pendingDatagramSize());
    QHostAddress from_ip;
    uint16_t from_port;
    _socket->readDatagram(buf.data(), buf.size(), &from_ip, &from_port);
    HostAddress from_addr = std::make_pair(from_ip.toString().toStdString(), from_port);

    if (buf[0] == '\x11' && buf[1] == '\x11'
        && buf[2] == '\x11' && buf[3] == '\x11') {

        if (auto p = _active_peers.key(from_addr); p.empty()) {
            this->make_holepunch(from_addr);
            std::string nickname {};
            for (qsizetype i = 4; i < buf.length(); i++) {
                if (buf[i] == '\x00') {
                    break;
                }
                nickname += buf[i];
            }
            _active_peers.insert(nickname, from_addr);
            qInfo() << "[INFO] Add new peer:" << nickname << from_ip.toString() << from_port;
            emit peer_registered(nickname);
        }
    } else {
        auto nickname = _active_peers.key(from_addr);
        _msg_storage.add_message(nickname, this->get_info().nickname, buf.toStdString());
        emit data_received(buf, nickname);
    }
}

void BasicPeer::ping_active_peers()
{
    for (const auto& peer : _active_peers) {
        this->make_holepunch(peer);
    }
}

void BasicPeer::register_peer(PeerInfo peer)
{
    switch (peer.nat_type) {
    case NatType::COMMON:
        this->make_holepunch(peer.address);
        break;
    case NatType::SYMMETRIC:
        this->make_holepunch(peer.address, true);
        break;
    }
}
