#pragma once

#include "net_common.h"

namespace RCNet
{
    namespace net
    {
        // Message Header structure
        template<typename T>
        struct message_header
        {
            T id{};
            uint32_t size = 0;
        };

        // Message structure
        template<typename T>
        struct message
        {
            // Header & Body vector
            message_header<T> header{};
            std::vector<uint8_t> body;

            size_t size() const
            {
                return body.size();
            }

            size_t entire_size() const
            {
                return sizeof(message_header<T>) + body.size();
            }

            // Override for std::cout compatibility - produces friendly description of message
            // friend 让“非成员函数”可以访问类的私有 / 受保护成员
            // 这里让 operator<< 这个“外部函数”，能直接访问 message<T> 的内部数据
            friend std::ostream &operator<<(std::ostream &os, const message<T> &msg)
            {
                os << "ID:" << int(msg.header.id) << " Size:" << msg.header.size;
                return os;
            }

            // Convenience Operator overloads - These allow us to add and remove stuff from
            // the body vector as if it were a stack, so First in, Last Out (Stack). These are a
            // template in itself, because we dont know what data type the user is pushing or
            // popping, so lets allow them all. NOTE: It assumes the data type is fundamentally
            // Plain Old Data (POD). TLDR: Serialise & Deserialize into/from a vector

            // POD: Plain Old Data，指的是那些可以直接通过内存拷贝进行传输和存储的数据类型，
            // e.g., 基本数据类型（int、float、char 等）和简单的结构体

            // Pushes any POD-like data into the message buffer
            // usage: msg << somePODData << someOtherPODData;
            template<typename DataType>
            friend message<T> &operator<<(message<T> &msg, const DataType &data)
            {
                // Check that the type of the data being pushed is trivially copyable
                static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pushed into vector");

                // Cache current size of vector, as this will be the point we insert the data
                size_t i = msg.body.size();

                // Resize the vector by the size of the data being pushed
                msg.body.resize(msg.body.size() + sizeof(DataType));

                // Physically copy the data into the newly allocated vector space
                // msg.body.data() + i => body 这块连续内存中，从第 i 个字节开始的位置
                std::memcpy(msg.body.data() + i, &data, sizeof(DataType));

                // Recalculate the message size
                msg.header.size = msg.size();

                // Return the target message so it can be "chained"
                return msg;
            }

            // Pulls any POD-like data form the message buffer
            // usage: msg >> someOtherPODData >> somePODData;
            template<typename DataType>
            friend message<T> &operator>>(message<T> &msg, DataType &data)
            {
                // Check that the type of the data being pushed is trivially copyable
                static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pulled from vector");

                // Cache the location towards the end of the vector where the pulled data starts
                size_t i = msg.body.size() - sizeof(DataType);

                // Physically copy the data from the vector into the user variable
                std::memcpy(&data, msg.body.data() + i, sizeof(DataType));

                // Shrink the vector to remove read bytes, and reset end position
                msg.body.resize(i);

                // Recalculate the message size
                msg.header.size = msg.size();

                // Return the target message so it can be "chained"
                return msg;
            }
        };

        // An "owned" message is identical to a regular message, but it is associated with
        // a connection. On a server, the owner would be the client that sent the message,
        // on a client the owner would be the server.

        // Forward declare the connection
        template<typename T>
        class connection;

        template<typename T>
        struct owned_message
        {
            std::shared_ptr<connection<T>> remote = nullptr;
            message<T> msg;

            // Again, a friendly string maker
            friend std::ostream &operator<<(std::ostream &os, const owned_message<T> &msg)
            {
                os << msg.msg;
                return os;
            }
        };
    }
}
