// ============================================================================
//  fsw/platform/posix/tcp_server_link.cpp
//  See tcp_server_link.hpp for why the spacecraft is the server and why
//  nothing here is allowed to block.
// ============================================================================
#include "platform/posix/tcp_server_link.hpp"

#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fsw::platform {

namespace {
// Put a descriptor into non-blocking mode. Every socket in this class goes
// through here; a single missed call would reintroduce the blocking behaviour
// the design exists to avoid.
bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { return false; }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
}  // namespace

TcpServerLink::TcpServerLink(uint16_t port) : port_(port) {}

TcpServerLink::~TcpServerLink() {
    close_peer();
    if (listen_fd_ >= 0) { ::close(listen_fd_); }
}

core::Status TcpServerLink::open() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) { return core::Status::IoError; }

    // Without SO_REUSEADDR, restarting the flight software within the TIME_WAIT
    // window fails to bind -- which during development is every single restart.
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(port_);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return core::Status::IoError;
    }
    if (::listen(listen_fd_, 1) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return core::Status::IoError;
    }
    if (!set_nonblocking(listen_fd_)) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return core::Status::IoError;
    }
    return core::Status::Ok;
}

void TcpServerLink::poll() {
    accept_pending();
    flush_tx();
}

void TcpServerLink::accept_pending() {
    if (listen_fd_ < 0) { return; }

    const int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) { return; }   // EAGAIN in the normal case: nobody waiting

    if (peer_fd_ >= 0) {
        // Already talking to someone. Refuse rather than silently switching
        // to a second controller.
        ::close(fd);
        return;
    }
    set_nonblocking(fd);

    // Disable Nagle. Telemetry packets are small and latency matters far more
    // than packing them together; with Nagle on, a 40-byte housekeeping packet
    // can sit in the kernel waiting for company.
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

#ifdef SO_NOSIGPIPE
    // macOS and the BSDs have no MSG_NOSIGNAL. Without this, writing to a
    // socket the ground has already closed raises SIGPIPE and kills the
    // flight software -- a ground tool being shut down must never be able to
    // take the spacecraft with it.
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif

    peer_fd_ = fd;
    tx_head_ = 0;
    tx_size_ = 0;
}

bool TcpServerLink::connected() const { return peer_fd_ >= 0; }

core::Status TcpServerLink::send(const uint8_t* data, size_t length) {
    if (peer_fd_ < 0) { return core::Status::Unavailable; }
    if (data == nullptr || length == 0) { return core::Status::Invalid; }

    if (tx_size_ + length > kTxQueueBytes) {
        // The link is not draining. Refuse the packet and count it: the caller
        // is in a far better position than this class to decide whether losing
        // this particular telemetry matters.
        ++tx_dropped_;
        return core::Status::NoSpace;
    }

    for (size_t i = 0; i < length; ++i) {
        tx_queue_[(tx_head_ + tx_size_ + i) % kTxQueueBytes] = data[i];
    }
    tx_size_ += length;

    flush_tx();
    return core::Status::Ok;
}

void TcpServerLink::flush_tx() {
    while (peer_fd_ >= 0 && tx_size_ > 0) {
        // The queue is circular, so write only up to the wrap point per call.
        const size_t contiguous =
            (tx_head_ + tx_size_ <= kTxQueueBytes) ? tx_size_ : (kTxQueueBytes - tx_head_);

        const ssize_t written = ::send(peer_fd_, tx_queue_ + tx_head_, contiguous,
#ifdef MSG_NOSIGNAL
                                       MSG_NOSIGNAL
#else
                                       0   // macOS: SO_NOSIGPIPE was set on accept instead
#endif
        );
        if (written > 0) {
            tx_head_ = (tx_head_ + static_cast<size_t>(written)) % kTxQueueBytes;
            tx_size_ -= static_cast<size_t>(written);
            bytes_sent_ += static_cast<uint64_t>(written);
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;   // socket full; try again next tick
        }
        close_peer();
        return;
    }
}

size_t TcpServerLink::receive(uint8_t* dst, size_t max_length) {
    if (peer_fd_ < 0 || dst == nullptr || max_length == 0) { return 0; }

    const ssize_t got = ::recv(peer_fd_, dst, max_length, 0);
    if (got > 0) {
        bytes_received_ += static_cast<uint64_t>(got);
        return static_cast<size_t>(got);
    }
    if (got == 0) {
        close_peer();   // orderly shutdown by the peer
        return 0;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        close_peer();
    }
    return 0;
}

void TcpServerLink::disconnect() { close_peer(); }

void TcpServerLink::close_peer() {
    if (peer_fd_ >= 0) {
        ::close(peer_fd_);
        peer_fd_ = -1;
    }
    tx_head_ = 0;
    tx_size_ = 0;
}

}  // namespace fsw::platform
