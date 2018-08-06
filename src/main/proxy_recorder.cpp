/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Product name: redemption, a FLOSS RDP proxy
   Copyright (C) Wallix 2018
   Author(s): David Fort

   A proxy that will capture all the traffic to the target
*/

#include "core/RDP/nla/nla.hpp"
#include "core/RDP/gcc.hpp"
#include "core/RDP/mcs.hpp"
#include "core/RDP/nego.hpp"
#include "core/RDP/tpdu_buffer.hpp"
#include "core/listen.hpp"
#include "core/server_notifier_api.hpp"
#include "core/session_reactor.hpp"
#include "main/version.hpp"
#include "transport/recorder_transport.hpp"
#include "transport/socket_transport.hpp"
#include "utils/cli.hpp"
#include "utils/fixed_random.hpp"
#include "utils/netutils.hpp"
#include "utils/redemption_info_version.hpp"
#include "utils/utf.hpp"

#include <vector>
#include <chrono>
#include <iostream>

#include <cerrno>
#include <cstring>
#include <csignal>

#include <netinet/tcp.h>
#include <sys/select.h>
#include <openssl/ssl.h>


using PacketType = RecorderFile::PacketType;


/// @brief Wrap a Transport and a RecorderFile for a nla negociation
struct NlaTeeTransport : Transport
{
    enum class Type : bool { Client, Server };

    NlaTeeTransport(Transport& trans, RecorderFile& outFile, Type type)
    : trans(trans)
    , outFile(outFile)
    , is_server(type == Type::Server)
    {}

    TlsResult enable_client_tls(
        bool server_cert_store,
        ServerCertCheck server_cert_check,
        ServerNotifier & server_notifier,
        const char * certif_path
    ) override
    {
        return this->trans.enable_client_tls(
            server_cert_store, server_cert_check, server_notifier, certif_path);
    }

    array_view_const_u8 get_public_key() const override
    {
        return this->trans.get_public_key();
    }

    bool disconnect() override
    {
        return this->trans.disconnect();
    }

    bool connect() override
    {
        return this->trans.connect();
    }

private:
    Read do_atomic_read(uint8_t * buffer, size_t len) override
    {
        auto r = this->trans.atomic_read(buffer, len);
        outFile.write_packet(
            this->is_server ? PacketType::NlaServerIn : PacketType::NlaClientIn,
            {buffer, len});
        return r;
    }

    size_t do_partial_read(uint8_t * buffer, size_t len) override
    {
        len = this->trans.partial_read(buffer, len);
        outFile.write_packet(
            this->is_server ? PacketType::NlaServerIn : PacketType::NlaClientIn,
            {buffer, len});
        return len;
    }

    void do_send(const uint8_t * buffer, size_t len) override
    {
        outFile.write_packet(
            this->is_server ? PacketType::NlaServerOut : PacketType::NlaClientOut,
            {buffer, len});
        this->trans.send(buffer, len);
    }

private:
    Transport& trans;
    RecorderFile& outFile;
    bool is_server;
};


/** @brief a front connection with a RDP client */
class FrontConnection
{
public:
    FrontConnection(unique_fd sck, std::string host, int port, std::string const& captureFile,
        std::string nla_username, std::string nla_password
    )
        : frontConn("front", std::move(sck), "127.0.0.1", 3389, std::chrono::milliseconds(100), to_verbose_flags(0))
        , backConn("back", ip_connect(host.c_str(), port, 3, 1000),
            host.c_str(), port, std::chrono::milliseconds(100), to_verbose_flags(0))
        , outFile(captureFile.c_str())
        , host(std::move(host))
        , nla_username(std::move(nla_username))
        , nla_password(std::move(nla_password))
    {
    }

    void treat_front_activity()
    {
        switch (state) {
        case NEGOCIATING_FRONT_STEP1:
            frontBuffer.load_data(this->frontConn);
            if (frontBuffer.next_pdu()) {
                array_view_u8 currentPacket = frontBuffer.current_pdu_buffer();
                InStream x224_stream(currentPacket);
                X224::CR_TPDU_Recv x224(x224_stream, true);
                if (x224._header_size != x224_stream.get_capacity()) {
                    LOG(LOG_WARNING,
                        "Front::incoming: connection request: all data should have been consumed,"
                        " %zu bytes remains",
                        x224_stream.get_capacity() - x224._header_size);
                }

                bool const is_nla
                  = (x224.rdp_neg_requestedProtocols & (X224::PROTOCOL_HYBRID | X224::PROTOCOL_HYBRID_EX));

                if (is_nla || !nla_password.empty()) {
                    StaticOutStream<256> new_x224_stream;
                    X224::CC_TPDU_Send(
                        new_x224_stream,
                        X224::RDP_NEG_RSP,
                        RdpNego::EXTENDED_CLIENT_DATA_SUPPORTED,
                        is_nla ? X224::PROTOCOL_HYBRID : X224::PROTOCOL_TLS);
                    outFile.write_packet(PacketType::DataIn, stream_to_avu8(new_x224_stream));
                    frontConn.send(stream_to_avu8(new_x224_stream));
                    frontConn.enable_server_tls("inquisition", nullptr);
                }

                if (is_nla) {
                    LOG(LOG_INFO, "start NegoServer");
                    nego_server = std::make_unique<NegoServer>(
                        frontConn, outFile, nla_username, nla_password);
                    nego_server->credssp.credssp_server_authenticate_init();

                    rdpCredsspServer::State st = rdpCredsspServer::State::Cont;
                    while (frontBuffer.next_credssp() && rdpCredsspServer::State::Cont == st) {
                        InStream in_stream(frontBuffer.current_pdu_buffer());
                        st = nego_server->credssp.credssp_server_authenticate_next(in_stream);
                    }

                    if (rdpCredsspServer::State::Err == st) {
                        throw Error(ERR_NLA_AUTHENTICATION_FAILED);
                    }

                    state = NEGOCIATING_FRONT_NLA;
                }
                else if (!nla_password.empty()) {
                    LOG(LOG_INFO, "start NegoClient");
                    nla_password.push_back('\0');
                    nego_client = std::make_unique<NegoClient>(
                        backConn, outFile,
                        host.c_str(), nla_username.c_str(), nla_password.c_str());
                    nego_client->send_negotiation_request();

                    state = NEGOCIATING_BACK_NLA;
                }
                else {
                    outFile.write_packet(PacketType::DataOut, currentPacket);
                    backConn.send(currentPacket);

                    state = NEGOCIATING_BACK_STEP1;
                }
            }
            break;

        case NEGOCIATING_FRONT_NLA: {
            rdpCredsspServer::State st = rdpCredsspServer::State::Cont;
            frontBuffer.load_data(this->frontConn);
            while (frontBuffer.next_credssp() && rdpCredsspServer::State::Cont == st) {
                InStream in_stream(frontBuffer.current_pdu_buffer());
                st = nego_server->credssp.credssp_server_authenticate_next(in_stream);
            }

            switch (st) {
            case rdpCredsspServer::State::Err:
                throw Error(ERR_NLA_AUTHENTICATION_FAILED);
            case rdpCredsspServer::State::Cont:
                break;
            case rdpCredsspServer::State::Finish:
                LOG(LOG_INFO, "stop NegoServer");
                LOG(LOG_INFO, "start NegoClient");
                nego_server.reset();
                nla_password.push_back('\0');
                nego_client = std::make_unique<NegoClient>(
                    this->backConn, this->outFile,
                    this->host.c_str(), nla_username.c_str(), nla_password.c_str());
                nego_client->send_negotiation_request();
                state = NEGOCIATING_BACK_STEP1;
                break;
            }

            break;
        }

        // force X224::PROTOCOL_HYBRID
        case NEGOCIATING_FRONT_INITIAL_PDU:
            frontBuffer.load_data(this->frontConn);
            if (frontBuffer.next_pdu()) {
                array_view_u8 currentPacket = frontBuffer.current_pdu_buffer();
                InStream new_x224_stream(currentPacket);
                X224::DT_TPDU_Recv x224(new_x224_stream);
                MCS::CONNECT_INITIAL_PDU_Recv mcs_ci(x224.payload, MCS::BER_ENCODING);
                GCC::Create_Request_Recv gcc_cr(mcs_ci.payload);
                GCC::UserData::RecvFactory f(gcc_cr.payload);
                GCC::UserData::CSCore cs_core;
                cs_core.recv(f.payload);

                outFile.write_packet(PacketType::DataOut, currentPacket);

                currentPacket[f.payload.get_current() - currentPacket.data() - 4] = X224::PROTOCOL_HYBRID;

                backConn.send(currentPacket);

                this->state = FORWARD;

                if (frontBuffer.next_pdu() || frontBuffer.remaining()) {
                    LOG(LOG_ERR, "frontBuffer isn't empty");
                    throw Error(ERR_RDP_INTERNAL);
                }
            }
            break;
        case FORWARD: {
            size_t ret = frontConn.partial_read(make_array_view(tmpBuffer));
            if (ret > 0) {
                outFile.write_packet(PacketType::DataOut, {tmpBuffer, ret});
                backConn.send(tmpBuffer, ret);
            }
            break;
        }
        case NEGOCIATING_BACK_STEP1:
        case NEGOCIATING_BACK_NLA:
            REDEMPTION_UNREACHABLE();
        }
    }

    void treat_back_activity()
    {
        switch (state) {
        case NEGOCIATING_BACK_NLA: {
            NullServerNotifier null_notifier;
            if (not nego_client->recv_next_data(backBuffer, null_notifier)) {
                LOG(LOG_INFO, "stop NegoClient");
                this->nego_client.reset();
                state = NEGOCIATING_FRONT_INITIAL_PDU;
                outFile.write_packet(PacketType::ClientCert, backConn.get_public_key());
            }
            break;
        }
        case NEGOCIATING_BACK_STEP1: {
            backBuffer.load_data(this->backConn);
            if (backBuffer.next_pdu()) {
                array_view_u8 currentPacket = backBuffer.current_pdu_buffer();
                outFile.write_packet(PacketType::DataIn, currentPacket);

                InStream x224_stream(currentPacket);
                X224::CC_TPDU_Recv x224(x224_stream);

                if (x224.rdp_neg_type == X224::RDP_NEG_NONE) {
                    LOG(LOG_INFO, "RdpNego::recv_connection_confirm done (legacy, no TLS)");
                }

                frontConn.send(currentPacket);

                switch (x224.rdp_neg_code) {
                case X224::PROTOCOL_TLS:
                case X224::PROTOCOL_HYBRID:
                case X224::PROTOCOL_HYBRID_EX: {
                    frontConn.enable_server_tls("inquisition", nullptr);
                    NullServerNotifier null_notifier;

                    switch(backConn.enable_client_tls(false, ServerCertCheck::always_succeed, null_notifier, "/tmp")) {
                    case Transport::TlsResult::Ok:
                        outFile.write_packet(PacketType::ClientCert, backConn.get_public_key());
                        break;
                    case Transport::TlsResult::Fail:
                        break;
                    case Transport::TlsResult::Want:
                        break;
                    }
                    break;
                }
                case X224::PROTOCOL_RDP: break;
                }

                state = FORWARD;
            }
            break;
        }
        case FORWARD: {
            size_t ret = backConn.partial_read(make_array_view(tmpBuffer));
            if (ret > 0) {
                frontConn.send(tmpBuffer, ret);
                outFile.write_packet(PacketType::DataIn, {tmpBuffer, ret});
            }
            break;
        }
        case NEGOCIATING_FRONT_NLA:
        case NEGOCIATING_FRONT_INITIAL_PDU:
        case NEGOCIATING_FRONT_STEP1:
            REDEMPTION_UNREACHABLE();
        }
    }

    void run()
    {
        fd_set rset;
        int const front_fd = frontConn.get_fd();
        int const back_fd = backConn.get_fd();

        for (;;) {
            FD_ZERO(&rset);

            switch(state) {
            case NEGOCIATING_FRONT_STEP1:
            case NEGOCIATING_FRONT_INITIAL_PDU:
            case NEGOCIATING_FRONT_NLA:
                FD_SET(front_fd, &rset);
                break;
            case NEGOCIATING_BACK_STEP1:
            case NEGOCIATING_BACK_NLA:
                FD_SET(back_fd, &rset);
                break;
            case FORWARD:
                FD_SET(front_fd, &rset);
                FD_SET(back_fd, &rset);
                break;
            }

            int status = select(std::max(front_fd, back_fd) + 1, &rset, nullptr, nullptr, nullptr);
            if (status < 0) {
                break;
            }

            if (FD_ISSET(front_fd, &rset)) {
                treat_front_activity();
            }

            if (FD_ISSET(back_fd, &rset)) {
                treat_back_activity();
            }
        }
    }

private:
    struct NegoServer
    {
        FixedRandom rand;
        LCGTime timeobj;
        std::string extra_message;
        NlaTeeTransport trans;
        rdpCredsspServer credssp;

        NegoServer(
            Transport& trans, RecorderFile& outFile,
            std::string const& user, std::string const& password)
        : trans(trans, outFile, NlaTeeTransport::Type::Server)
        , credssp(
            this->trans, false, false, rand, timeobj, extra_message, Translation::EN,
            [&](SEC_WINNT_AUTH_IDENTITY& identity){
                auto check = [vec = std::vector<uint8_t>{}](
                    std::string const& str, Array& arr
                ) mutable {
                    vec.resize(str.size() * 2);
                    UTF8toUTF16(byte_ptr_cast(str.data()), vec.data(), vec.size());
                    return vec.size() == arr.size()
                        && 0 == memcmp(vec.data(), arr.get_data(), vec.size());
                };

                if (check(user, identity.User)
                 && check({}, identity.Domain)
                ) {
                    identity.SetPasswordFromUtf8(byte_ptr_cast(password.c_str()));
                    return true;
                }

                LOG(LOG_ERR, "Ntlm: bad identity");
                return false;
            })
        {}
    };

    class NegoClient
    {
        LCGTime timeobj;
        FixedRandom random;
        std::string extra_message;
        RdpNego nego;
        NlaTeeTransport trans;

    public:
        NegoClient(
            Transport& trans, RecorderFile& outFile,
            char const* host, char const* target_user, char const* password
        )
        : nego(true, target_user, true, false, host, false,
            this->random, this->timeobj, this->extra_message, Translation::EN)
        , trans(trans, outFile, NlaTeeTransport::Type::Client)
        {
            char const* separator = strchr(target_user, '\\');
            std::string username;
            std::string domain;
            if (separator) {
                username = separator+1;
                domain.assign(target_user, separator-target_user);
            }
            else {
                separator = strchr(target_user, '@');
                if (separator) {
                    username.assign(target_user, separator-target_user);
                    domain = separator+1;
                }
                else {
                    username = target_user;
                }
            }
            nego.set_identity(username.c_str(), domain.c_str(), password, "ProxyRecorder");
            // static char ln_info[] = "tsv://MS Terminal Services Plugin.1.Sessions";
            // nego.set_lb_info(byte_ptr_cast(ln_info), sizeof(ln_info)-1);
        }

        void send_negotiation_request()
        {
            this->nego.send_negotiation_request(this->trans);
        }

        bool recv_next_data(TpduBuffer& tpdu_buffer, ServerNotifier& notifier)
        {
            return this->nego.recv_next_data(
                tpdu_buffer, this->trans,
                RdpNego::ServerCert{
                    false, ServerCertCheck::always_succeed, "/tmp", notifier}
            );
        }
    };

    enum {
        NEGOCIATING_FRONT_STEP1,
        NEGOCIATING_BACK_STEP1,
        NEGOCIATING_BACK_NLA,
        NEGOCIATING_FRONT_INITIAL_PDU,
        NEGOCIATING_FRONT_NLA,
        FORWARD
    } state = NEGOCIATING_FRONT_STEP1;

    SocketTransport frontConn;
    SocketTransport backConn;

    RecorderFile outFile;

    uint8_t tmpBuffer[0xffff];

    TpduBuffer frontBuffer;
    TpduBuffer backBuffer;

    std::unique_ptr<NegoClient> nego_client;
    std::unique_ptr<NegoServer> nego_server;

    std::string host;
    std::string nla_username;
    std::string nla_password;
};

/** @brief the server that handles RDP connections */
class FrontServer : public Server
{
public:
    FrontServer(std::string host, int port, std::string captureFile, std::string nla_username, std::string nla_password)
        : targetPort(port)
        , targetHost(std::move(host))
        , captureTemplate(std::move(captureFile))
        , nla_username(std::move(nla_username))
        , nla_password(std::move(nla_password))
    {
        // just ignore this signal because there is no child termination management yet.
        struct sigaction sa;
        sa.sa_flags = 0;
        sigaddset(&sa.sa_mask, SIGCHLD);
        sa.sa_handler = SIG_IGN; /*NOLINT*/
        sigaction(SIGCHLD, &sa, nullptr);
    }

    Server::Server_status start(int sck, bool forkable) override {
        unique_fd sck_in {accept(sck, nullptr, nullptr)};
        if (!sck_in) {
            LOG(LOG_INFO, "Accept failed on socket %d (%s)", sck, strerror(errno));
            _exit(1);
        }

        const pid_t pid = forkable ? fork() : 0;
        connection_counter++;

        if(pid == 0) {
            close(sck);

            int nodelay = 1;
            if (setsockopt(sck_in.fd(), IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
                LOG(LOG_ERR, "Failed to set socket TCP_NODELAY option on client socket");
                _exit(1);
            }

            char finalPathBuffer[256];
            char const* finalPath = captureTemplate.format(finalPathBuffer, connection_counter);
            LOG(LOG_INFO, "Recording front connection in %s", finalPath);

            FrontConnection conn(
                std::move(sck_in), targetHost, targetPort, finalPath,
                nla_username, nla_password);
            try {
                conn.run();
            } catch(Error const& e) {
                if (errno) {
                    LOG(LOG_ERR, "Recording front connection ending: %s ; %s", e.errmsg(), strerror(errno));
                }
                else {
                    LOG(LOG_ERR, "Recording front connection ending: %s", e.errmsg());
                }
                exit(1);
            }
            exit(0);
        }
        else if (!forkable) {
            return Server::START_FAILED;
        }

        return Server::START_OK;
    }

private:
    struct CaptureTemplate
    {
        std::string captureTemplate;
        int startDigit = 0;
        int endDigit = 0;

        CaptureTemplate(std::string captureFile)
            : captureTemplate(std::move(captureFile))
        {
            std::string::size_type pos = 0;
            while ((pos = captureTemplate.find('%', pos)) != std::string::npos) {
                if (captureTemplate[pos+1] == 'd') {
                    startDigit = int(pos);
                    endDigit = startDigit + 2;
                    break;
                }
                ++pos;
            }
        }

        char const* format(array_view_char path, int counter) const
        {
            if (endDigit) {
                std::snprintf(path.data(), path.size(), "%.*s%04d%s",
                    startDigit, captureTemplate.c_str(),
                    counter,
                    captureTemplate.c_str() + endDigit
                );
                path.back() = '\0';
                return path.data();
            }

            return captureTemplate.c_str();
        }
    };

    int connection_counter = 0;
    int targetPort;
    std::string targetHost;
    CaptureTemplate captureTemplate;
    std::string nla_username;
    std::string nla_password;
};


struct CliPassword
{
    cli::Res operator()(cli::ParseResult& pr) const
    {
        if (!pr.str) {
            return cli::Res::BadFormat;
        }

        char* s = av[pr.opti];
        password = s;
        // hide password in /proc/...
        for (int i = 0; *s; ++s, ++i) {
            *s = (i < 3) ? '*' : '\0';
        }
        ++pr.opti;

        return cli::Res::Ok;
    }

    std::string& password;
    char ** av;
};

template<class Output, class Opt>
void print_action(Output&& out, Opt const& /*unused*/, CliPassword const& /*clipass*/)
{
    out << " [password]";
}

int main(int argc, char *argv[])
{
    char const* target_host = nullptr;
    int target_port = 3389;
    int listen_port = 3389;
    char const* capture_file = nullptr;
    std::string nla_username;
    std::string nla_password;
    bool forkable = true;

    auto options = cli::options(
        cli::option('h', "help").help("Show help").action(cli::help),
        cli::option('v', "version").help("Show version")
            .action(cli::quit([]{ std::cout << "ProxyRecorder 1.0, " << redemption_info_version() << "\n"; })),
        cli::option('s', "target-host").action(cli::arg_location("host", target_host)),
        cli::option('p', "target-port").action(cli::arg_location("port", target_port)),
        cli::option('P', "port").help("Listen port").action(cli::arg_location(listen_port)),
        cli::option("nla-username").action(cli::arg_location("username", nla_username)),
        cli::option("nla-password").action(CliPassword{nla_password, argv}),
        cli::option('t', "template").help("Ex: dump-%d.out")
            .action(cli::arg_location("path", capture_file)),
        cli::option('N', "no-fork").action([&]{ forkable = false; })
    );

    auto cli_result = cli::parse(options, argc, argv);
    switch (cli_result.res) {
        case cli::Res::Ok:
            break;
        case cli::Res::Exit:
            return 0;
        case cli::Res::Help:
            cli::print_help(options, std::cout);
            return 0;
        case cli::Res::BadFormat:
        case cli::Res::BadOption:
            std::cerr << "Bad " << (cli_result.res == cli::Res::BadFormat ? "format" : "option") << " at parameter " << cli_result.opti;
            if (cli_result.opti < cli_result.argc) {
                std::cerr << " (" << cli_result.argv[cli_result.opti] << ")";
            }
            std::cerr << "\n";
            return 1;
    }

    if (!target_host) {
        std::cerr << "Missing --target-host\n";
    }

    if (!capture_file) {
        std::cerr << "Missing --template\n";
    }

    if (!target_host || !capture_file) {
        cli::print_help(options, std::cerr << "\n");
        return 1;
    }

    SSL_library_init();

    openlog("FrontConnection", LOG_CONS | LOG_PERROR, LOG_USER);

    FrontServer front(
        target_host, target_port, capture_file,
        std::move(nla_username), std::move(nla_password));
    Listen listener(front, inet_addr("0.0.0.0"), listen_port);
    if (listener.sck <= 0) {
        return 2;
    }
    listener.run(forkable);
}