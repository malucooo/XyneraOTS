// Copyright 2022 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "connection.h"

#include "configmanager.h"
#include "outputmessage.h"
#include "protocol.h"
#include "server.h"
#include "tasks.h"

extern ConfigManager g_config;

Connection_ptr ConnectionManager::createConnection(boost::asio::io_service& io_service, ConstServicePort_ptr servicePort)
{
	std::lock_guard<std::mutex> lockClass(connectionManagerLock);

	auto connection = std::make_shared<Connection>(io_service, servicePort);
	connections.insert(connection);
	return connection;
}

void ConnectionManager::releaseConnection(const Connection_ptr& connection)
{
	std::lock_guard<std::mutex> lockClass(connectionManagerLock);

	connections.erase(connection);
}

void ConnectionManager::closeAll()
{
	std::lock_guard<std::mutex> lockClass(connectionManagerLock);

	for (const auto& connection : connections) {
		try {
			boost::system::error_code error;
			connection->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
			connection->socket.close(error);
		} catch (boost::system::system_error&) {
		}
	}
	connections.clear();
}

// Connection

void Connection::close(bool force)
{
	//any thread
	ConnectionManager::getInstance().releaseConnection(shared_from_this());

	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	connectionState = CONNECTION_STATE_DISCONNECTED;
#ifdef DEBUG_DISCONNECT
	console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] connection state: Disconnected");
#endif

	if (protocol) {
		g_dispatcher.addTask(createTask([protocol = protocol]() { protocol->release(); }));
	}

	if (messageQueue.empty() || force) {
		closeSocket();
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Disconnected (code 24)");
#endif
	} else {
		//will be closed by the destructor or onWriteOperation
	}
}

void Connection::closeSocket()
{
	if (socket.is_open()) {
		try {
			readTimer.cancel();
			writeTimer.cancel();
			boost::system::error_code error;
			socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
			socket.close(error);
#ifdef DEBUG_DISCONNECT
			console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Disconnected (code 25)");
#endif
		} catch (boost::system::system_error& e) {
			console::reportError("Connection::closeSocket", fmt::format("Network error: {:s}", e.what()));
		}
	}
}

Connection::~Connection()
{
#ifdef DEBUG_DISCONNECT
	console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Disconnected (code 26)");
#endif
	closeSocket();
}

void Connection::accept(Protocol_ptr protocol)
{
	this->protocol = protocol;
	g_dispatcher.addTask(createTask(([=]() { protocol->onConnect(); })));
	connectionState = CONNECTION_STATE_GAMEWORLD_AUTH;
#ifdef DEBUG_DISCONNECT
	console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] connection state: gameworld auth");
#endif
	accept();
}

void Connection::accept()
{
	if (connectionState == CONNECTION_STATE_PENDING) {
		connectionState = CONNECTION_STATE_REQUEST_CHARLIST;
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] connection state: Charlist");
#endif
	}

	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	try {
		readTimer.expires_from_now(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait([thisPtr = std::weak_ptr<Connection>(shared_from_this())](const boost::system::error_code& error) { Connection::handleTimeout(thisPtr, error); });

		// Read size of the first packet
		auto bufferLength = !receivedLastChar && receivedName && connectionState == CONNECTION_STATE_GAMEWORLD_AUTH ? 1 : NetworkMessage::HEADER_LENGTH;
		boost::asio::async_read(socket,
								boost::asio::buffer(msg.getBuffer(), bufferLength),
								[thisPtr = shared_from_this()](const boost::system::error_code& error, auto /*bytes_transferred*/) { thisPtr->parseHeader(error); });
	} catch (boost::system::system_error& e) {
		console::reportError("Connection::accept", fmt::format("Network error: {:s}", e.what()));
		close(FORCE_CLOSE);
	}
}

void Connection::parseHeader(const boost::system::error_code& error)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	readTimer.cancel();

	if (error) {
		close(FORCE_CLOSE);
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Disconnected (code 1)");
#endif
		return;
	} else if (connectionState == CONNECTION_STATE_DISCONNECTED) {
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Packet skipped (code 2)");
#endif
		return;
	}

	uint32_t timePassed = std::max<uint32_t>(1, (time(nullptr) - timeConnected) + 1);
	if ((++packetsSent / timePassed) > static_cast<uint32_t>(g_config.getNumber(ConfigManager::MAX_PACKETS_PER_SECOND))) {
		console::print(CONSOLEMESSAGE_TYPE_INFO, fmt::format("{:s} disconnected for exceeding packet per second limit.", convertIPToString(getIP()))); //, true, "Connection::parseHeader");
		close();
		return;
	}

	if (!receivedLastChar && connectionState == CONNECTION_STATE_GAMEWORLD_AUTH) {
		uint8_t* msgBuffer = msg.getBuffer();

		// read world name
		if (!receivedName && msgBuffer[1] == 0x00) {
			receivedLastChar = true;
		} else {
			if (!receivedName) {
				receivedName = true;
#ifdef DEBUG_DISCONNECT
				console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Reading world name (code 38)");
#endif
				accept();
				return;
			}

			// header of next expected packet
			if (msgBuffer[0] == 0x0A) {
				receivedLastChar = true;
			}

#ifdef DEBUG_DISCONNECT
			if (!receivedLastChar) {
				console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Parsing world name (code 39)");
			}
#endif

			accept();
			return;
		}
	}

	if (receivedLastChar && connectionState == CONNECTION_STATE_GAMEWORLD_AUTH) {
		connectionState = CONNECTION_STATE_GAME;
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] connection state: Game");
#endif
	}

	if (timePassed > 2) {
		timeConnected = time(nullptr);
		packetsSent = 0;
	}

	uint16_t size = (msg.getLengthHeader() * 8) + 4;
	if (size == 0 || size >= NETWORKMESSAGE_MAXSIZE - 16) {
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Disconnected (code 3)");
#endif
		close(FORCE_CLOSE);
		return;
	}

	try {
		readTimer.expires_from_now(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait([thisPtr = std::weak_ptr<Connection>(shared_from_this())](const boost::system::error_code& error) { Connection::handleTimeout(thisPtr, error); });

		// Read packet content
		msg.setLength(size + NetworkMessage::HEADER_LENGTH);
		boost::asio::async_read(socket, boost::asio::buffer(msg.getBodyBuffer(), size),
								[thisPtr = shared_from_this()](const boost::system::error_code& error, auto /*bytes_transferred*/) { thisPtr->parsePacket(error); });
	} catch (boost::system::system_error& e) {
		console::reportError("Connection::parseHeader", fmt::format("Network error: {:s}", e.what()));
		close(FORCE_CLOSE);
	}
}

void Connection::parsePacket(const boost::system::error_code& error)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	readTimer.cancel();

	if (error) {
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Disconnected (code 4)");
#endif
		close(FORCE_CLOSE);
		return;
	} else if (connectionState == CONNECTION_STATE_DISCONNECTED) {
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Packet skipped (code 5)");
#endif
		return;
	}

	// Read potential checksum bytes
	msg.get<uint32_t>();

	if (!receivedFirst) {
		receivedFirst = true;

		if (!protocol) {
			// Skip deprecated checksum bytes (with clients that aren't using it in mind)
			uint16_t len = msg.getLength();
			if (len < 280 && len != 151) {
				msg.skipBytes(-NetworkMessage::CHECKSUM_LENGTH);
			}

			// Game protocol has already been created at this point
			protocol = service_port->make_protocol(msg, shared_from_this());
			if (!protocol) {
#ifdef DEBUG_DISCONNECT
				console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Disconnected (code 6)");
#endif
				close(FORCE_CLOSE);
				return;
			}
		} else {
			msg.skipBytes(2); // skip padding count
		}

		protocol->onRecvFirstMessage(msg);
	} else {
		protocol->onRecvMessage(msg); // Send the packet to the current protocol
	}

	try {
		readTimer.expires_from_now(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait([thisPtr = std::weak_ptr<Connection>(shared_from_this())](const boost::system::error_code& error) { Connection::handleTimeout(thisPtr, error); });

		// Wait to the next packet
		boost::asio::async_read(socket,
								boost::asio::buffer(msg.getBuffer(), NetworkMessage::HEADER_LENGTH),
								[thisPtr = shared_from_this()](const boost::system::error_code& error, auto /*bytes_transferred*/) { thisPtr->parseHeader(error); });
	} catch (boost::system::system_error& e) {
		console::reportError("Connection::parsePacket", fmt::format("Network error: {:s}", e.what()));
		close(FORCE_CLOSE);
	}
}

void Connection::send(const OutputMessage_ptr& msg)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	if (connectionState == CONNECTION_STATE_DISCONNECTED) {
		return;
	}

	bool noPendingWrite = messageQueue.empty();
	messageQueue.emplace_back(msg);
	if (noPendingWrite) {
		internalSend(msg);
	}
}

void Connection::internalSend(const OutputMessage_ptr& msg)
{
	protocol->onSendMessage(msg);
	try {
		writeTimer.expires_from_now(std::chrono::seconds(CONNECTION_WRITE_TIMEOUT));
		writeTimer.async_wait([thisPtr = std::weak_ptr<Connection>(shared_from_this())](const boost::system::error_code& error) { Connection::handleTimeout(thisPtr, error); });

		boost::asio::async_write(socket,
								boost::asio::buffer(msg->getOutputBuffer(), msg->getLength()),
								[thisPtr = shared_from_this()](const boost::system::error_code& error, auto /*bytes_transferred*/) { thisPtr->onWriteOperation(error); });
	} catch (boost::system::system_error& e) {
		console::reportError("Connection::internalSend", fmt::format("Network error: {:s}", e.what()));
		close(FORCE_CLOSE);
	}
}

uint32_t Connection::getIP()
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);

	// IP-address is expressed in network byte order
	boost::system::error_code error;
	const boost::asio::ip::tcp::endpoint endpoint = socket.remote_endpoint(error);
	if (error) {
		return 0;
	}

	return htonl(endpoint.address().to_v4().to_ulong());
}

void Connection::onWriteOperation(const boost::system::error_code& error)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	writeTimer.cancel();
	messageQueue.pop_front();

	if (error) {
		messageQueue.clear();
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Disconnected (code 7)");
#endif
		close(FORCE_CLOSE);
		return;
	}

	if (!messageQueue.empty()) {
		internalSend(messageQueue.front());
	} else if (connectionState == CONNECTION_STATE_DISCONNECTED) {
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Socket closed (code 8)");
#endif
		closeSocket();
	}
}

void Connection::handleTimeout(ConnectionWeak_ptr connectionWeak, const boost::system::error_code& error)
{
	if (error == boost::asio::error::operation_aborted) {
		// The timer has been cancelled manually
		return;
	}

	if (auto connection = connectionWeak.lock()) {
#ifdef DEBUG_DISCONNECT
		console::print(CONSOLEMESSAGE_TYPE_INFO, "[DEBUG] Timeout (code 9)");
#endif
		connection->close(FORCE_CLOSE);
	}
}
