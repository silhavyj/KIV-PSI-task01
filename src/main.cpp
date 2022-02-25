#include <iostream>
#include <cstring>
#include <cstdint>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "cxxopts.hpp"

namespace psi {
    static constexpr const char *SSDP_DISCOVERY_MSG =
            "M-SEARCH * HTTP/1.1\r\n"
            "HOST: 239.255.255.250:1900\r\n"
            "MAN: \"ssdp:discover\"\r\n"
            "MX: 2\r\n"
            "ST: ssdp:all\r\n\r\n";

    static constexpr uint32_t BUFFER_SIZE = 1024;
    static constexpr uint16_t SSDP_PORT = 1900;
    static constexpr const char *DEFAULT_NETWORK_INTERFACE = "0.0.0.0";
    static constexpr const char *SSDP_MULTICAST_IP = "239.255.255.250";
    static constexpr const char *DEFAULT_MAX_SCAN_SECONDS = "5";
    static constexpr uint32_t SECONDS_SOCKETS_TIMEOUT_US = 10000;
}

int main(int argc, char *argv[]) {
    // Create a simple description of the application
    cxxopts::ParseResult arg;
    cxxopts::Options options("./kiv-psi-task01-silhavyj", "SSDP client (Sends out a discovery message and prints out responses)");

    // Add a list of all options the application can be run with.
    options.add_options()
            ("i,inet", "IP address the server will be bound to", cxxopts::value<std::string>()->default_value(psi::DEFAULT_NETWORK_INTERFACE))
            ("t,timeout", "Number of seconds for which the application will be waiting for responses", cxxopts::value<uint32_t>()->default_value(psi::DEFAULT_MAX_SCAN_SECONDS))
            ("h,help" , "Prints help")
            ;

    // Parse all arguments the user passed into the application.
    // If they've entered 'help', print out the help menu and terminate the application.
    arg = options.parse(argc, argv);
    if (arg.count("help")) {
        std::cout << options.help() << std::endl;
        return EXIT_SUCCESS;
    }

    uint32_t timeout = arg["timeout"].as<uint32_t>();
    std::string inetIP = arg["inet"].as<std::string>();

    int udpSocket;                       // UDP socket
    int reuse = 1;                       // enable reuse of the socket (multiple instances of the application)
    char loopBack = 0;                   // disable loop backs
    char buffer[psi::BUFFER_SIZE];       // buffer (received messages)

    struct sockaddr_in deviceAddress {}; // used for receiving messages from devices
    struct sockaddr_in groupAddress {};  // init of the multicast group
    struct sockaddr_in localAddress {};  // port & ip the socket will be bound on
    struct in_addr localInterface {};    // local interface the socket will send its datagrams on
    struct ip_mreq group {};             // joining the multicast group (ip of the group + ip of the interface)

    socklen_t deviceAddressLength = sizeof(deviceAddress);

    // Create a UDP socket.
    if ((udpSocket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        std::cout << "ERR: failed to create a UDP socket\n";
        exit(EXIT_FAILURE);
    }

    // Allow multiple instances of the application to receive the same packets.
    if (setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse), sizeof(reuse)) == -1) {
        std::cout << "ERR: failed to allow SO_REUSEADDR\n";
        exit(EXIT_FAILURE);
    }

    // Disable loop backs, so we won't receive our own messages.
    if (setsockopt(udpSocket, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<char *>(&loopBack), sizeof(loopBack)) == -1) {
        std::cout << "ERR: failed to disable IP_MULTICAST_LOOP\n";
        exit(EXIT_FAILURE);
    }

    // Initialize the multicast group (IP + port)
    groupAddress.sin_family = AF_INET;
    groupAddress.sin_addr.s_addr = inet_addr(psi::SSDP_MULTICAST_IP);
    groupAddress.sin_port = htons(psi::SSDP_PORT);

    // Initialize local interface for outbound multicast diagrams.
    localInterface.s_addr = inet_addr(inetIP.c_str());

    // Tell the socket which interface to send its multicast packets on.
    if (setsockopt(udpSocket, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<char *>(&localInterface), sizeof(localInterface)) == -1) {
        std::cout << "ERR: failed specifying the interface the socket would send its data on\n";
        exit(EXIT_FAILURE);
    }

    // Port number and IP address the socket will be bound to.
    localAddress.sin_family = AF_INET;
    localAddress.sin_port = htons(psi::SSDP_PORT);
    localAddress.sin_addr.s_addr = INADDR_ANY;

    // Bind the UDP socket to a specific IP address & port.
    if (bind(udpSocket, reinterpret_cast<struct sockaddr *>(&localAddress), sizeof(localAddress)) == -1) {
        std::cout << "ERR: failed to bind the socket\n";
        exit(EXIT_FAILURE);
    }

    // Join the multicast group on a specified interface.
    group.imr_multiaddr.s_addr = inet_addr(psi::SSDP_MULTICAST_IP);
    group.imr_interface.s_addr = inet_addr(inetIP.c_str());

    if (setsockopt(udpSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char *>(&group), sizeof(group)) == -1) {
        std::cout << "ERR: failed to join the multicast group\n";
        exit(EXIT_FAILURE);
    }

    // Send the SSDP discovery message.
    ssize_t sentBytes = sendto(udpSocket, psi::SSDP_DISCOVERY_MSG, strlen(psi::SSDP_DISCOVERY_MSG), 0, reinterpret_cast<struct sockaddr *>(&groupAddress), sizeof(groupAddress));
    if (sentBytes == -1) {
        std::cout << "ERR: SSDP discovery message did not go through\n";
        exit(EXIT_FAILURE);
    }

    ssize_t receivedBytes;
    fd_set sockets;
    FD_ZERO(&sockets);
    struct timeval socketTimeout{};

    time_t start = time(nullptr);
    while ((time(nullptr) - start) < timeout) {
        FD_SET(udpSocket, &sockets);
        socketTimeout.tv_sec  = 0;
        socketTimeout.tv_usec = psi::SECONDS_SOCKETS_TIMEOUT_US;
        select(FD_SETSIZE, &sockets, nullptr, nullptr, &socketTimeout);

        // Check if there's data to be read from the socket
        if (FD_ISSET(udpSocket, &sockets)) {
            receivedBytes = recvfrom(udpSocket, buffer, psi::BUFFER_SIZE - 1, 0, reinterpret_cast<struct sockaddr *>(&deviceAddress),&deviceAddressLength);
            if (receivedBytes != -1) {
                buffer[receivedBytes] = '\0';
                std::cout << buffer << '\n';
            }
        }
    }
    return EXIT_SUCCESS;
}