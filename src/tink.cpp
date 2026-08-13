#include "tink.h"

#include <QDebug>
#include <QThread>

Tink::Tink(QObject *parent)
    : QObject(parent)
{
    connect(&m_serial, &QSerialPort::readyRead, this, &Tink::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &Tink::onSerialError);

    m_responseTimer.setSingleShot(true);
    connect(&m_responseTimer, &QTimer::timeout, this, &Tink::onResponseTimeout);
}

Tink::~Tink()
{
    cleanup();
}

void Tink::cleanup()
{
    m_responseTimer.stop();
    if (m_fwFile.isOpen()) {
        m_fwFile.close();
        qInfo().noquote() << QStringLiteral("Closed firmware file");
    }
    if (m_serial.isOpen()) {
        m_serial.close();
        qInfo().noquote() << QStringLiteral("Closed serial port");
    }
    m_blState = BlIdle;
    m_rxState = RxIdle;
}

quint16 Tink::calcCrc(const QByteArray &data)
{
    // CRC lookup table for polynomial 0x1021
    static const quint16 lut[16] = {
        0, 4129, 8258, 12387,
        16516, 20645, 24774, 28903,
        33032, 37161, 41290, 45419,
        49548, 53677, 57806, 61935,
    };

    quint16 crc = 0;
    for (quint8 b : data) {
        quint8 n = (crc >> 12) ^ (b >> 4);
        quint16 tmp = lut[n & 0x0F] ^ quint16(crc << 4);
        n = quint8((tmp >> 12) ^ b);
        crc = lut[n & 0x0F] ^ quint16(tmp << 4);
    }
    return crc;
}

bool Tink::validateHexFile()
{
    // Ensure the file exists, has valid Intel Hex checksums, and count lines
    QFile file(m_fwName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qInfo().noquote() << QStringLiteral("Could not open %1").arg(m_fwName);
        return false;
    }

    m_hexNline = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;
        m_hexNline++;

        const QByteArray record = QByteArray::fromHex(line.mid(1));
        if (record.isEmpty()) {
            qInfo().noquote() << QStringLiteral("%1 is not a valid hex file").arg(m_fwName);
            return false;
        }

        quint8 sum = 0;
        for (int i = 0; i < record.size() - 1; i++)
            sum += quint8(record.at(i));
        const quint8 checksum = quint8((~sum + 1) & 0xFF);

        if (checksum != quint8(record.back())) {
            qInfo().noquote() << QStringLiteral("%1 is not a valid hex file").arg(m_fwName);
            return false;
        }
    }

    return m_hexNline > 0;
}

void Tink::start(const QString &fwName, const QString &portName)
{
    if (isRunning())
        return;

    m_fwName = fwName;
    if (!validateHexFile()) {
        emit finished(false);
        return;
    }

    m_serial.setPortName(portName);
    m_serial.setBaudRate(QSerialPort::Baud115200);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::HardwareControl);

    if (!m_serial.open(QIODevice::ReadWrite)) {
        qInfo().noquote() << QStringLiteral("Could not open device at %1").arg(portName);
        qInfo().noquote() << QStringLiteral("Error: %1").arg(m_serial.errorString());
        emit finished(false);
        return;
    }
    qInfo().noquote() << QStringLiteral("Opened device at %1").arg(portName);

    m_hexLine = 0;
    m_nextCheckpoint = CheckpointStep;
    m_rxState = RxIdle;
    m_rxBuf.clear();
    emit progress(0);

    qInfo().noquote() << QStringLiteral("Probing device...");
    m_blState = BlVersion;
    txPacket(QByteArray(1, CmdGetVer));
}

QByteArray Tink::buildFrame(const QByteArray &payload)
{
    QByteArray packet = payload;
    const quint16 crc = calcCrc(payload);
    packet.append(char(crc & 0xFF));
    packet.append(char((crc >> 8) & 0xFF));

    QByteArray tx(1, SOH);
    for (char b : packet) {
        // Escape any control characters that appear in the TX buffer
        if (b == SOH || b == EOT || b == DLE)
            tx.append(DLE);
        tx.append(b);
    }
    tx.append(EOT);
    return tx;
}

void Tink::txPacket(const QByteArray &payload)
{
    if (m_serial.isOpen()) {
        m_serial.write(buildFrame(payload));
        m_serial.flush();
        // Give the retrotink a moment to breathe between frames
        QThread::msleep(1);
        // A full flash erase can take a long time (tinkup.py waited
        // forever); only probe/write acks arrive quickly.
        m_responseTimer.start(m_blState == BlErase ? EraseTimeoutMs
                                                   : ResponseTimeoutMs);
    } else {
        qInfo().noquote() << QStringLiteral("TX failure, serial port not writeable");
    }
}

void Tink::onReadyRead()
{
    const QByteArray data = m_serial.readAll();
    for (char b : data)
        rxByte(b);
}

void Tink::rxByte(char b)
{
    switch (m_rxState) {
    case RxIdle:
        // Ignore bytes until we see SOH
        if (b == SOH) {
            m_rxBuf.clear();
            m_rxState = RxBuffer;
        }
        break;

    case RxBuffer:
        if (b == DLE) {
            // Escape the next control sequence
            m_rxState = RxEscape;
        } else if (b == EOT) {
            // End of transmission
            m_rxState = RxIdle;
            rxProcess(m_rxBuf);
        } else {
            m_rxBuf.append(b);
        }
        break;

    case RxEscape:
        // Unconditionally buffer any byte following the escape sequence
        m_rxBuf.append(b);
        m_rxState = RxBuffer;
        break;
    }
}

void Tink::rxProcess(const QByteArray &packet)
{
    if (packet.size() < 3) {
        fail(QStringLiteral("Runt packet received"));
        return;
    }

    m_responseTimer.stop();

    const quint16 crcRx = (quint8(packet.at(packet.size() - 1)) << 8)
                        | quint8(packet.at(packet.size() - 2));
    if (calcCrc(packet.left(packet.size() - 2)) != crcRx) {
        fail(QStringLiteral("Bad CRC received, resetting state"));
        return;
    }

    const char cmd = packet.at(0);
    const QByteArray payload = packet.mid(1, packet.size() - 3);

    switch (m_blState) {
    case BlVersion:
        if (cmd == CmdGetVer) {
            const QString deviceId = QString::fromLatin1(payload).section(QChar(u'\0'), 0, 0);
            qInfo().noquote() << QStringLiteral("Found device ID: %1").arg(deviceId);

            qInfo().noquote() << QStringLiteral("Erasing device...");
            m_blState = BlErase;
            txPacket(QByteArray(1, CmdErase));
        } else {
            fail(QStringLiteral("ERROR: Expected response code CmdGetVer, got %1").arg(int(cmd)));
        }
        break;

    case BlErase:
        if (cmd == CmdErase) {
            qInfo().noquote() << QStringLiteral("OKAY");

            if (!m_fwFile.isOpen()) {
                m_fwFile.setFileName(m_fwName);
                if (!m_fwFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    fail(QStringLiteral("Could not reopen %1").arg(m_fwName));
                    return;
                }
            }
            m_hexLine = 1;
            m_blState = BlWrite;
            emit writingStarted();
            sendNextWrite();
        } else {
            fail(QStringLiteral("ERROR: Expected response code CmdErase, got %1").arg(int(cmd)));
        }
        break;

    case BlWrite:
        if (cmd == CmdWrite) {
            qInfo().noquote() << QStringLiteral("OKAY");
            m_hexLine++;

            // m_hexLine starts at 1, so we send up to and including m_hexNline
            if (m_hexLine > m_hexNline) {
                qInfo().noquote() << QStringLiteral("Update complete, booting firmware");
                m_blState = BlJump;
                txPacket(QByteArray(1, JumpApp));
                // There doesn't seem to be a response to the JumpApp
                // command, so at this point we're done.
                m_responseTimer.stop();
                cleanup();
                emit finished(true);
            } else {
                sendNextWrite();
            }
        } else {
            fail(QStringLiteral("ERROR: Expected response code CmdWrite, got %1").arg(int(cmd)));
        }
        break;

    default:
        break;
    }
}

bool Tink::sendNextWrite()
{
    const QByteArray line = m_fwFile.readLine().trimmed();
    if (line.isEmpty()) {
        fail(QStringLiteral("Unexpected end of firmware file"));
        return false;
    }

    QByteArray tx(1, CmdWrite);
    tx += QByteArray::fromHex(line.mid(1));

    qInfo().noquote() << QStringLiteral("Writing firmware %1/%2...").arg(m_hexLine).arg(m_hexNline);

    // Report progress only at CheckpointStep checkpoints of transmitted frames
    const int percent = m_hexLine * 100 / m_hexNline;
    if (percent >= m_nextCheckpoint) {
        const int checkpoint = percent - percent % CheckpointStep;
        emit progress(checkpoint);
        m_nextCheckpoint = checkpoint + CheckpointStep;
    }

    txPacket(tx);
    return true;
}

void Tink::onResponseTimeout()
{
    if (isRunning())
        fail(QStringLiteral("Timed out waiting for device response"));
}

void Tink::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;
    if (isRunning())
        fail(QStringLiteral("Serial port error: %1").arg(m_serial.errorString()));
}

void Tink::fail(const QString &reason)
{
    qInfo().noquote() << reason;
    cleanup();
    emit finished(false);
}
