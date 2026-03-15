#include <iostream>
#include "rc_net.h"
#include "net_client.h"
#include <windows.h>

#include <windows.h>

static bool PollConsoleKey(char& outKey)
{
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) return false;

    DWORD numEvents = 0;
    if (!GetNumberOfConsoleInputEvents(hIn, &numEvents) || numEvents == 0)
        return false;

    INPUT_RECORD rec;
    DWORD read = 0;

    // 读掉非 KeyEvent，直到遇到 KeyEvent 或者缓冲空
    while (numEvents-- > 0)
    {
        if (!PeekConsoleInputA(hIn, &rec, 1, &read) || read == 0)
            return false;

        // 真正把它从缓冲区取走
        ReadConsoleInputA(hIn, &rec, 1, &read);

        if (rec.EventType == KEY_EVENT)
        {
            const KEY_EVENT_RECORD& k = rec.Event.KeyEvent;

            // 只响应 KeyDown
            if (k.bKeyDown)
            {
                // 这里拿到的是虚拟键码/ASCII 字符
                // 对数字键 '1' '2' '3'，用 AsciiChar 最直观
                char ch = k.uChar.AsciiChar;
                if (ch == '1' || ch == '2' || ch == '3')
                {
                    outKey = ch;
                    return true;
                }
            }
        }
    }

    return false;
}


class CustomClient : public RCNet::net::client_interface<CustomMsgTypes>
{
public:
    void PingServer()
    {
        RCNet::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::ServerPing;

        // Record current time
        // caution: client and host might not be in the same computer and have different system time!
        std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();

        // send current time to server
        msg << timeNow;
        Send(msg);
    }

    void MessageAll()
    {
        RCNet::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::MessageAll;
        Send(msg);
        std::cout << "Send message to other clients.\n";
    }
};

int main()
{
    CustomClient c;
    c.Connect("127.0.0.1", 60000);

    bool bQuit = false;

    while (!bQuit)
    {
        char key;
        while (PollConsoleKey(key)) // 这一帧把缓冲里所有按键事件都处理掉
        {
            if (key == '1') c.PingServer();
            if (key == '2') c.MessageAll();
            if (key == '3') bQuit = true;
        }

        if (c.IsConnected())
        {
            if (!c.Incoming().empty())
            {
                auto msg = c.Incoming().pop_front().msg;

                switch (msg.header.id)
                {
                    case CustomMsgTypes::ServerAccept:
                    {
                        std::cout << "Server Accepted Connection.\n";
                    }
                    break;

                    case CustomMsgTypes::ServerPing:
                    {
                        // Server has responded to a ping request
                        std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();
                        std::chrono::system_clock::time_point timeThen;
                        msg >> timeThen;
                        std::cout << "Ping: " << std::chrono::duration<double>(timeNow - timeThen).count() << "\n";
                    }
                    break;

                    case CustomMsgTypes::ServerMessage:
                    {
                        // Server has responded to a ping request	
                        uint32_t clientID;
                        msg >> clientID;
                        std::cout << "Hello from [" << clientID << "]\n";
                    }
                    break;
                }
            }
        } else
        {
            std::cout << "Server Down\n";
            bQuit = true;
        }

        Sleep(10);
    }

    return 0;
}
