#pragma once

#include "net_common.h"
#include "net_thread_safe_queue.h"
#include "net_message.h"
#include "net_server.h"

namespace RCNet
{
    namespace net
    {
        // forward declare
        template <typename T>
        class server_interface;

        // enable_shared_from_this: allows us to get a shared pointer to this
        template<typename T>
        class connection : public std::enable_shared_from_this<connection<T>>
        {
        public:
            // A connection is "owned" by either a server or a client, and its
            // behaviour is slightly different between the two.
            enum class owner
            {
                server,
                client
            };

        public :
            // Constructor: Specify Owner, connect to context, transfer the socket
            //				Provide reference to incoming message queue
            connection(owner parent, asio::io_context &asioContext, asio::ip::tcp::socket socket,
                       tsqueue<owned_message<T>> &qIn)
                : m_socket(std::move(socket)), m_asioContext(asioContext), m_qMessagesIn(qIn)
            {
                m_nOwnerType = parent;

                // Construct validation check data
                if (m_nOwnerType == owner::server)
                {
                    // Connection is Server -> Client, construct random data for the client
                    // to transform and send back for validation
                    m_nHandshakeOut = uint64_t(std::chrono::system_clock::now().time_since_epoch().count());

                    // Pre-calculate the result for checking when the client responds
                    m_nHandshakeCheck = scramble(m_nHandshakeOut);
                } else
                {
                    // Connection is Client -> Server, so we have nothing to define,
                    m_nHandshakeIn = 0;
                    m_nHandshakeOut = 0;
                }
            }

            virtual ~connection()
            {
            }

            // This ID is used system-wide - It's how clients will understand other clients
            // exist across the whole system.
            uint32_t GetID() const
            {
                return id;
            }

        public:
            void ConnectToClient(RCNet::net::server_interface<T> *server, uint32_t uid = 0)
            {
                // Only servers can connect to clients
                if (m_nOwnerType == owner::server)
                {
                    if (m_socket.is_open())
                    {
                        id = uid;

                        // Was: ReadHeader();

                        // A client has attempted to connect to the server, but we wish
                        // the client to first validate itself, so first write out the
                        // handshake data to be validated
                        WriteValidation();

                        // Next, issue a task to sit and wait asynchronously for precisely
                        // the validation data sent back from the client
                        ReadValidation(server);
                    }
                }
            }

            void ConnectToServer(const asio::ip::tcp::resolver::results_type &endpoints)
            {
                // Only clients can connect to servers
                if (m_nOwnerType == owner::client)
                {
                    // Request asio attempts to connect to an endpoint
                    asio::async_connect(m_socket, endpoints,
                                        [this](std::error_code ec, asio::ip::tcp::endpoint endpoint)
                                        {
                                            if (!ec)
                                            {
                                                // Was: ReadHeader();

                                                // First thing server will do is send packet to be validated
                                                // so wait for that and respond
                                                ReadValidation();
                                            }
                                        });
                }
            }

            void Disconnect()
            {
                if (IsConnected())
                    asio::post(m_asioContext, [this]() { m_socket.close(); });
            }

            bool IsConnected() const
            {
                return m_socket.is_open();
            }

            // Prime the connection to wait for incoming messages
            void StartListening()
            {
            }

        public:
            // ASYNC - Send a message, connections are one-to-one so no need to specify
            // the target, for a client, the target is the server and vice versa
            void Send(const message<T> &msg)
            {
                // send a job(via lambda function) to the asio context, to do the actual sending of the message
                asio::post(m_asioContext,
                           [this, msg]()
                           {
                               // If the queue has a message in it, then we must
                               // assume that it is in the process of asynchronously being written.
                               // Either way(不论发生哪一种情况) add the message to the queue to be output. If no messages
                               // were available to be written, then start the process of writing the
                               // message at the front of the queue.
                               // 即：如果队列中已经有消息了，说明正在异步写入消息
                               // 必须要等到当前队列内的所有消息写入完成后（队列为空），才能继续写入新消息
                               bool bWritingMessage = m_qMessagesOut.empty();
                               m_qMessagesOut.push_back(msg);
                               if (bWritingMessage) // m_qMessagesOut was empty before we added our message
                               {
                                   WriteHeader();
                               }
                           });
            }

        private:
            // ASYNC - Prime context ready to read a message header
            void ReadHeader()
            {
                asio::async_read(m_socket, asio::buffer(&m_msgTemporaryIn.header, sizeof(message_header<T>)),
                                 [this](std::error_code ec, std::size_t length)
                                 {
                                     if (!ec)
                                     {
                                         if (m_msgTemporaryIn.header.size > 0)
                                         {
                                             // Allocate appropriate space in the message body
                                             m_msgTemporaryIn.body.resize(m_msgTemporaryIn.header.size);
                                             ReadBody();
                                         } else
                                         {
                                             // bodyless message, just add to incoming queue
                                             AddToIncomingMessageQueue();
                                         }
                                     } else
                                     {
                                         // Reading form the client went wrong, most likely a disconnect has occurred.
                                         // Close the socket and let the system tidy it up later.
                                         std::cout << "[" << id << "] Read Header Fail.\n";
                                         m_socket.close();
                                     }
                                 });
            }

            // ASYNC - Prime context ready to read a message body
            void ReadBody()
            {
                asio::async_read(m_socket, asio::buffer(m_msgTemporaryIn.body.data(), m_msgTemporaryIn.body.size()),
                                 [this](std::error_code ec, std::size_t length)
                                 {
                                     // If this function is called, a header has already been read, and that header
                                     // request we read a body, The space for that body has already been allocated
                                     // in the temporary message object, so just wait for the bytes to arrive...
                                     if (!ec)
                                     {
                                         // ...and they have! The message is now complete, so add
                                         // the whole message to incoming queue
                                         AddToIncomingMessageQueue();
                                     } else
                                     {
                                         std::cout << "[" << id << "] Read Body Fail.\n";
                                         m_socket.close();
                                     }
                                 });
            }

            // ASYNC - Prime context to write a message header
            void WriteHeader()
            {
                asio::async_write(m_socket, asio::buffer(&m_qMessagesOut.front().header, sizeof(message_header<T>)),
                                  [this](std::error_code ec, std::size_t length)
                                  {
                                      if (!ec)
                                      {
                                          if (m_qMessagesOut.front().body.size() > 0)
                                          {
                                              WriteBody();
                                          } else
                                          {
                                              m_qMessagesOut.pop_front();

                                              // If the queue still has messages in it, then issue another write
                                              if (!m_qMessagesOut.empty())
                                              {
                                                  WriteHeader();
                                              }
                                          }
                                      } else
                                      {
                                          // ...asio failed to write the message, we could analyse why but
                                          // for now simply assume the connection has died by closing the
                                          // socket. When a future attempt to write to this client fails due
                                          // to the closed socket, it will be tidied up.
                                          std::cout << "[" << id << "] Write Header Fail.\n";
                                          m_socket.close();
                                      }
                                  });
            }

            // ASYNC - Prime context to write a message body
            void WriteBody()
            {
                asio::async_write(
                    m_socket, asio::buffer(m_qMessagesOut.front().body.data(), m_qMessagesOut.front().body.size()),
                    [this](std::error_code ec, std::size_t length)
                    {
                        if (!ec)
                        {
                            m_qMessagesOut.pop_front();

                            if (!m_qMessagesOut.empty())
                            {
                                WriteHeader();
                            }
                        } else
                        {
                            // Sending failed, see WriteHeader() equivalent for description :P
                            std::cout << "[" << id << "] Write Body Fail.\n";
                            m_socket.close();
                        }
                    });
            }

            // Once a full message is received, add it to the incoming queue
            void AddToIncomingMessageQueue()
            {
                // Shove it in queue, converting it to an "owned message", by initialising
                // with the shared pointer from this connection object
                if (m_nOwnerType == owner::server)
                    m_qMessagesIn.push_back({this->shared_from_this(), m_msgTemporaryIn});
                else
                    // If we are a client, the "owner" is simply nullptr
                    m_qMessagesIn.push_back({nullptr, m_msgTemporaryIn});

                // We must now prime the asio context to receive the next message. It
                // will just sit and wait for bytes to arrive, and the message construction
                // process repeats itself. Clever huh?
                ReadHeader();
            }

        private:
            // Encrypt / Scramble (for validation)
            uint64_t scramble(uint64_t nInput)
            {
                // 加 ULL 是为了强制这个字面量是 64 位无符号整数，否则可能类型不对、被截断或发生未定义行为
                // Step 1: XOR with constant
                uint64_t out = nInput ^ 0xDEADBEEF0DECAFEULL;

                // Step 2: swap high/low nibbles in each byte
                out = ((out & 0xF0F0F0F0F0F0F0F0ULL) >> 4) |
                      ((out & 0x0F0F0F0F0F0F0F0FULL) << 4);

                // Step 3: XOR with another constant
                return out ^ 0xC0DEFACE12345678ULL;
            }

            // ASYNC - Used by both client and server to write validation packet
            void WriteValidation()
            {
                asio::async_write(m_socket, asio::buffer(&m_nHandshakeOut, sizeof(uint64_t)),
                                  [this](std::error_code ec, std::size_t length)
                                  {
                                      if (!ec)
                                      {
                                          // Validation data sent, clients should sit and wait
                                          // for a response (or a closure)
                                          if (m_nOwnerType == owner::client)
                                              ReadHeader();
                                      } else
                                      {
                                          m_socket.close();
                                      }
                                  });
            }

            void ReadValidation(RCNet::net::server_interface<T> *server = nullptr)
            {
                asio::async_read(m_socket, asio::buffer(&m_nHandshakeIn, sizeof(uint64_t)),
                                 [this, server](std::error_code ec, std::size_t length)
                                 {
                                     if (!ec)
                                     {
                                         if (m_nOwnerType == owner::server)
                                         {
                                             // Connection is a server, so check response from client

                                             // Compare sent data to actual solution
                                             if (m_nHandshakeIn == m_nHandshakeCheck)
                                             {
                                                 // Client has provided valid solution, so allow it to connect properly
                                                 std::cout << "Client Validated" << std::endl;
                                                 server->OnClientValidated(this->shared_from_this());

                                                 // Sit waiting to receive data now
                                                 ReadHeader();
                                             } else
                                             {
                                                 // Client gave incorrect data, so disconnect
                                                 std::cout << "Client Disconnected (Fail Validation)" << std::endl;
                                                 m_socket.close();
                                             }
                                         } else
                                         {
                                             // Connection is a client, so solve puzzle
                                             m_nHandshakeOut = scramble(m_nHandshakeIn);

                                             // Write the result
                                             WriteValidation();
                                         }
                                     } else
                                     {
                                         // Some biggerfailure occured
                                         std::cout << "Client Disconnected (ReadValidation)" << std::endl;
                                         m_socket.close();
                                     }
                                 });
            }

        protected:
            asio::ip::tcp::socket m_socket;

            // this context is shared with the whole asio instance
            asio::io_context &m_asioContext;

            // This queue holds all messages to be sent to the remote side
            // of this connection
            tsqueue<message<T>> m_qMessagesOut;

            // This references the incoming queue of the parent object (server or client)
            tsqueue<owned_message<T>> &m_qMessagesIn;

            // Incoming messages are constructed asynchronously, so we will
            // store the part assembled message here, until it is ready
            message<T> m_msgTemporaryIn;

            // The "owner" decides how some of the connection behaves
            owner m_nOwnerType = owner::server;

            uint32_t id = 0;

            // Handshake Validation
            uint64_t m_nHandshakeOut = 0;
            uint64_t m_nHandshakeIn = 0;
            uint64_t m_nHandshakeCheck = 0;
        };
    }
}
