/**
  @file cppwebserver.cpp
  @author Herik Lima
*/

#include "cppwebserver.h"
#include "configuration.h"

namespace CWF
{
    extern Configuration configuration;

    CppWebServer::CppWebServer(Filter *filter) : filter(filter)
    {
        loadSslConfiguration();
        this->thread()->setPriority(QThread::TimeCriticalPriority);

        if(this->filter == nullptr)
            this->filter = new Filter;
        QThreadPool::globalInstance()->setMaxThreadCount(configuration.maxThread);
        QThreadPool::globalInstance()->setExpiryTimeout(configuration.timeOut);
        timer = new QTimer;
        connect(timer, SIGNAL(timeout()), this, SLOT(doClean()));
        timer->start(configuration.cleanupInterval);       
    }

    CppWebServer::~CppWebServer()
    {
        while(!QThreadPool::globalInstance()->waitForDone());
        for(QMapThreadSafety<QString, HttpServlet*>::iterator it = urlServlet.begin(); it != urlServlet.end(); ++it)
        {
            HttpServlet *o = it.value();
            if(o != nullptr)
            {
                delete o;
                o = nullptr;
            }
        }

        for(QMapThreadSafety<QString, HttpSession*>::iterator it = sessions.begin(); it != sessions.end(); ++it)
        {
            HttpSession *o = it.value();
            if(o != nullptr)
            {
                delete o;
                o = nullptr;
            }
        }

        delete filter;
        if(sslConfiguration)
            delete sslConfiguration;
    }

    void CppWebServer::addUrlServlet(const QString &url, HttpServlet *servlet)
    {
        urlServlet.insert(url, servlet);
    }

    void CppWebServer::incomingConnection(qintptr socketfd)
    {
        while(block)
            this->thread()->msleep(sleepTime);

        HttpReadRequest *read = new HttpReadRequest(socketfd,
                                                    urlServlet,
                                                    sessions,
                                                    sslConfiguration,
                                                    filter);
        read->setAutoDelete(true);
        QThreadPool::globalInstance()->start(read, QThread::TimeCriticalPriority);
    }

    void CppWebServer::doClean()
    {
        QMutex mutex;
        mutex.lock();
        block = true;
        mutex.unlock();
        while(!QThreadPool::globalInstance()->waitForDone(sleepTime));

        HttpSession *session = nullptr;
        QStringList idSessionsToDelete;
        for(auto it = sessions.begin(); it != sessions.end(); ++it)
        {
            session = it.value();
            if(session->isExpired())
            {
                idSessionsToDelete.push_back(session->getId());
                delete session;
            }
        }
        for(int i = 0; i < idSessionsToDelete.size(); ++i)
            sessions.remove(idSessionsToDelete[i]);

        mutex.lock();
        block = false;
        mutex.unlock();
    }

    void CppWebServer::loadSslConfiguration()
    {
        QString sslKey  = configuration.sslKeyFile;
        QString sslCert = configuration.sslCertFile;
        if (!sslKey.isEmpty() && !sslCert.isEmpty())
        {
            #ifdef QT_NO_OPENSSL
                qDebug() << "SSL is not supported";
                return;
            #else
                QFile certFile(sslCert);
                QFile keyFile(sslKey);

                if(!certFile.open(QIODevice::ReadOnly))
                {
                    qDebug() << "Can't open SSL Cert File";
                    return;
                }
                if(!keyFile.open(QIODevice::ReadOnly))
                {
                    qDebug() << "Can't open SSL Key File";
                    return;
                }

                QSslCertificate certificate(&certFile, QSsl::Pem);
                QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
                keyFile.close();
                certFile.close();

                if(key.isNull())
                {
                    qDebug() << "Invalid SLL Key File";
                    return;
                }
                else if(certificate.isNull())
                {
                    qDebug() << "Invalid SLL Cert File";
                    return;
                }

                sslConfiguration = new QSslConfiguration();

                sslConfiguration->setLocalCertificate(certificate);
                sslConfiguration->setPrivateKey(key);
                sslConfiguration->setPeerVerifyMode(QSslSocket::VerifyNone);
                sslConfiguration->setProtocol(QSsl::TlsV1SslV3);
             #endif
        }
    }
}
