////////////////////////////////////////////////////////////////////////////////
// Includes                                                                   //
////////////////////////////////////////////////////////////////////////////////
#include "NDKSocket.h"
#include "NDKServer.h"

#include <assert.h>
#include <iostream>
using namespace std;
using namespace DRAFramework;

//#define NONBLOCK_SOKCET

#include "../TaskDefines.h"

CNDKSocket::CNDKSocket()
{
    m_lpStreamSocket    = NULL;
    m_lpServer          = NULL;
    m_bIsRunning        = true;
}

// Constructor with initialization.
CNDKSocket::CNDKSocket(Poco::Net::StreamSocket* lpStreamSocket, CNDKServer *lpServer)
{
    m_lpStreamSocket    = lpStreamSocket;
    m_lpServer          = lpServer;
    m_bIsRunning        = true;
}

// Destructor.
CNDKSocket::~CNDKSocket()
{
    Close();
}

////////////////////////////////////////////////////////////////////////////////
// Attributes                                                                 //
////////////////////////////////////////////////////////////////////////////////

// Return if the buffer is empty.
bool CNDKSocket::IsBufferEmpty() const
{
    bool bResult = true;

    if (m_lpStreamSocket)
        bResult = m_lpStreamSocket->available() > 0 ? false : true;

    return bResult;
}


bool CNDKSocket::Initialize()
{
    if (m_lpStreamSocket) {
#ifdef NONBLOCK_SOKCET
        // blocking mode �� ���̺귯 ������ send �Լ� ȣ��� ��Ȥ shced_yield ȣ��������
        // Accept ������ ��� �񵿱� ó���ϴ� ���� ���� ������
        m_lpStreamSocket->setBlocking(false);
#endif
        // ���ۿ� �׾Ƶ��� �ʰ� ���� ��� �ٷ� �۽�
        m_lpStreamSocket->setNoDelay(true);
        // ť�� ���̸� DRCF �� "out of memory" �� �״°� ����(Ÿ�Ӿƿ� ����)
        Poco::Timespan sendTimeout(1, 0); // 1��
        m_lpStreamSocket->setSendTimeout(sendTimeout);
    }
    return true;
}

void CNDKSocket::Close()
{
    if ( !m_lpStreamSocket)
        return;

    try {
        m_lpStreamSocket->close();
    } catch (Poco::Exception &exc) {
        CTaskUtil::DebugStrinFrwSocket("%s\n", exc.displayText().c_str());
    } catch(std::exception& exc) {
        CTaskUtil::DebugStrinFrwSocket("%s\n", exc.what());
    } catch (...) {
        CTaskUtil::DebugStrinFrwSocket("Unknown Exception\n");
    }

    m_lpStreamSocket = NULL;
}

void CNDKSocket::ShutDown()
{
    m_bIsRunning = false;

    if ( !m_lpStreamSocket)
        return;

    try {
        m_lpStreamSocket->shutdown();
    } catch (Poco::Exception &exc) {
        CTaskUtil::DebugStrinFrwSocket("%s\n", exc.displayText().c_str());
    } catch(std::exception& exc) {
        CTaskUtil::DebugStrinFrwSocket("%s\n", exc.what());
    } catch (...) {
        CTaskUtil::DebugStrinFrwSocket("Unknown Exception\n");
    }
}

// return IP address of remote client
void CNDKSocket::GetPeerAddress(std::string& rIpAddress, unsigned int& rPort)
{
    if (m_lpStreamSocket != NULL) {

        Poco::Net::SocketAddress sa = m_lpStreamSocket->peerAddress();

        rIpAddress = sa.host().toString();
        rPort      = sa.port();
    }
}

// Sends a message.
bool CNDKSocket::SendMessage(CNDKMessage& message)
{
    bool bResult =false;

    if ( !m_lpStreamSocket) {
        return bResult;
    }

    int iTotalByteSend = message.GetLength();
    int iTotalByteSent = 0;
    do
    {
        try {
            int iByteSent = m_lpStreamSocket->sendBytes((message.GetBuffer() + iTotalByteSent), iTotalByteSend - iTotalByteSent);
            if (iByteSent > 0) {
                iTotalByteSent += iByteSent;
                bResult = true;
            }
            else{
                bResult = false;
            }

        } catch (Poco::Exception &exc) {
#ifdef NONBLOCK_SOKCET
            CTaskUtil::DebugString("%s\n", exc.displayText().c_str());

            if (exc.code() == POCO_EINTR || exc.code() == POCO_EWOULDBLOCK) {
                bResult = true;
            }
            else {
                bResult = false;
            }
#else
            CTaskUtil::DebugStrinFrwSocket("%s\n", exc.displayText().c_str());
            bResult = false;
#endif
        } catch(std::exception& exc) {
            CTaskUtil::DebugStrinFrwSocket("%s\n", exc.what());
            bResult = false;
        } catch (...) {
            CTaskUtil::DebugStrinFrwSocket("Unknown Exception\n");
            bResult = false;
        }
    } while (bResult && iTotalByteSent != iTotalByteSend);

    return bResult;
}

// Receive a message.
bool CNDKSocket::ReceiveMessage(CNDKMessage& message)
{
    bool bResult = false;

    if( !m_lpStreamSocket) {
        return bResult;
    }
    unsigned char szBuffer[READ_BUFFER_SIZE + 1] = {'\0', };

    try {
        int iBytesMessage = m_lpStreamSocket->receiveBytes(szBuffer, READ_BUFFER_SIZE);
        if (iBytesMessage > 0) {
            message.SetBuffer(szBuffer, iBytesMessage);
            bResult = true;
        }
        else {
            bResult = false;
        }
    } catch (Poco::Exception &exc) {
#ifdef NONBLOCK_SOKCET
        CTaskUtil::DebugString("%s\n", exc.displayText().c_str());

        if (exc.code() == POCO_EINTR || exc.code() == POCO_EWOULDBLOCK) {
            bResult = true;
        }
        else {
            bResult = false;
        }
#else
        CTaskUtil::DebugStrinFrwSocket("%s\n", exc.displayText().c_str());
        bResult = false;
#endif
    } catch(std::exception& exc) {
        CTaskUtil::DebugStrinFrwSocket("%s\n", exc.what());
        bResult = false;
    } catch (...) {
        CTaskUtil::DebugStrinFrwSocket("Unknown Exception\n");
        bResult = false;
    }

    return bResult;
}

void CNDKSocket::run()
{
    bool bResult = true;
    Poco::Timespan recvTimeout(1, 0); // 1��

    while (m_bIsRunning) {

        try {
            if (m_lpStreamSocket->poll(recvTimeout, Poco::Net::Socket::SELECT_READ)) {
                // send �Լ� ���н� shutdown ������� ���� ���۰� ���� ������
                // recevie �Լ����� ���ۿ��� ���� ������ ���� LOCK �ɸ��� ���� ������.
                if (!m_bIsRunning) break;

                OnReceive(0);
            }
        } catch (Poco::Exception &exc) {
            CTaskUtil::DebugStrinFrwSocket("%s\n", exc.displayText().c_str());
            bResult = false;
        } catch(std::exception& exc) {
            CTaskUtil::DebugStrinFrwSocket("%s\n", exc.what());
            bResult = false;
        } catch (...) {
            CTaskUtil::DebugStrinFrwSocket("Unknown Exception\n");
            bResult = false;
        }

        if ( !bResult) break;
    }
}

#ifdef __XENO__
int  CNDKServerConnectionFactory::m_iRTTask = 0;
#endif

Poco::Net::TCPServerConnection*
    CNDKServerConnectionFactory::createConnection(const Poco::Net::StreamSocket& StreamSocket)
{
#ifdef __XENO__
    char szTaskName[256] = {'\0', };
    sprintf(szTaskName, "siTcpClient#%d", m_iRTTask);

    if (rt_task_self() == 0) {
        rt_task_shadow(&m_hRTTask[m_iRTTask++], szTaskName, PRIORITY_SOCKET, T_CPU(USR_CPU));
        m_iRTTask = m_iRTTask % 10;
    }
#endif
    // no need to delete, automatically gets deleted magically by TCPServerConnectionFactory
    CNDKServerSocket* lpServerSocket = new CNDKServerSocket(StreamSocket, m_lpServer);
    lpServerSocket->OnAccept(0);

    return (Poco::Net::TCPServerConnection*)lpServerSocket;
}

// Called when a new connection attempts to connect.
void CNDKServerSocket::OnAccept(int nErrorCode)
{
    CNDKSocket::OnAccept(nErrorCode);

    assert(m_lpServer != NULL);

    if (m_lpServer != NULL) {

        m_lpServer->ProcessPendingAccept(this, nErrorCode);
    }
}

// Called when data is received.
void CNDKServerSocket::OnReceive(int nErrorCode)
{
    CNDKSocket::OnReceive(nErrorCode);

    assert(m_lpServer != NULL);

    if (m_lpServer != NULL)
    {
        m_lpServer->ProcessPendingRead(this, nErrorCode);
    }
}
